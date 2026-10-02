#include "cpu_snapshot.hpp"
#include <algorithm>
#include <bit>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <drm_fourcc.h>
#include <fcntl.h>
#include <iostream>
#include <linux/dma-buf.h>
#include <spa/buffer/buffer.h>
#include <spa/param/video/format.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#ifdef CLOUDPLAY_HAVE_PNG
#include <png.h>
#endif

namespace cloudplay::capture {
namespace {
[[noreturn]] void error(const char *stage, int code = 0) {
    throw FrameCaptureError(FrameCaptureFailure::UnsupportedFormat, stage, code);
}
int sync_cpu(int fd, std::uint64_t flags) noexcept {
    dma_buf_sync sync{flags};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
    while (ioctl(fd, DMA_BUF_IOCTL_SYNC, &sync) < 0) {
        const int code = errno;
        if ((code != EINTR && code != EAGAIN) || std::chrono::steady_clock::now() >= deadline)
            return code;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return 0;
}
void validate(const CapturedFrame &frame) {
    if constexpr (std::endian::native != std::endian::little)
        error("snapshot.unsupported_byte_order");
    if ((frame.storage != FrameStorage::DmaBuf && frame.storage != FrameStorage::CpuMemory) ||
        frame.plane_count != 1 ||
        (frame.storage == FrameStorage::DmaBuf && frame.modifier != DRM_FORMAT_MOD_LINEAR))
        error("snapshot.requires_single_plane_linear_dmabuf");
    if (frame.width == 0 || frame.height == 0 || frame.width > 8192 || frame.height > 8192 ||
        frame.planes[0].stride < static_cast<std::int64_t>(frame.width) * 4)
        error("snapshot.invalid_geometry_or_stride");
    if (frame.drm_format != DRM_FORMAT_XRGB8888 && frame.drm_format != DRM_FORMAT_ARGB8888 &&
        frame.drm_format != DRM_FORMAT_XBGR8888 && frame.drm_format != DRM_FORMAT_ABGR8888)
        error("snapshot.unsupported_fourcc");
    const auto &d = frame.diagnostics;
    if (d.transform != 0 || (d.crop_present && d.crop_width && d.crop_height &&
                             (d.crop_x != 0 || d.crop_y != 0 || d.crop_width != frame.width ||
                              d.crop_height != frame.height)))
        error("snapshot.nonidentity_crop_or_transform");
}
} // namespace

CpuImage unpack_linear_frame(const CapturedFrame &frame, std::span<const unsigned char> memory) {
    validate(frame);
    const auto &plane = frame.planes[0];
    const auto last = static_cast<std::uint64_t>(plane.offset) +
                      static_cast<std::uint64_t>(frame.height - 1) * plane.stride +
                      static_cast<std::uint64_t>(frame.width) * 4;
    if (last > memory.size())
        error("snapshot.plane_out_of_bounds");
    CpuImage image{
        frame.width, frame.height,
        std::vector<unsigned char>(static_cast<std::size_t>(frame.width) * frame.height * 3)};
    const bool bgr =
        frame.drm_format == DRM_FORMAT_XRGB8888 || frame.drm_format == DRM_FORMAT_ARGB8888;
    for (std::uint32_t y = 0; y < frame.height; ++y) {
        const auto *row = memory.data() + plane.offset + static_cast<std::size_t>(y) * plane.stride;
        for (std::uint32_t x = 0; x < frame.width; ++x) {
            const auto src = static_cast<std::size_t>(x) * 4;
            const auto dst = (static_cast<std::size_t>(y) * frame.width + x) * 3;
            image.rgb[dst] = row[src + (bgr ? 2 : 0)];
            image.rgb[dst + 1] = row[src + 1];
            image.rgb[dst + 2] = row[src + (bgr ? 0 : 2)];
        }
    }
    return image;
}

CpuImage read_cpu_snapshot(const CapturedFrame &frame) {
    validate(frame);
    if (frame.storage == FrameStorage::CpuMemory) {
        if (!frame.cpu_data || !frame.cpu_size)
            error("snapshot.cpu_plane_unavailable");
        return unpack_linear_frame(frame, {frame.cpu_data, frame.cpu_size});
    }
    if (!frame.diagnostics.implicit_fences_ready || frame.diagnostics.explicit_sync_present)
        error("snapshot.fence_not_verified");
    const int fd = frame.planes[0].fd;
    struct stat status {};
    if (fstat(fd, &status) < 0)
        error("snapshot.fstat", errno);
    if (status.st_size <= 0 || status.st_size > 256 * 1024 * 1024)
        error("snapshot.invalid_allocation_size");
    auto size = static_cast<std::size_t>(status.st_size);
    void *mapped = mmap(nullptr, size, PROT_READ, MAP_SHARED, fd, 0);
    if (mapped == MAP_FAILED)
        error("snapshot.mmap", errno);
    const FrameLease mapping(&size, mapped, [](void *owner, void *buffer) noexcept {
        munmap(buffer, *static_cast<const std::size_t *>(owner));
    });
    if (const auto code = sync_cpu(fd, DMA_BUF_SYNC_START | DMA_BUF_SYNC_READ))
        error("snapshot.sync_start", code);
    int end_error{};
    CpuImage image;
    {
        struct Access {
            int fd;
            int *error;
        } access{fd, &end_error};
        const FrameLease sync(&access, nullptr, [](void *owner, void *) noexcept {
            auto &value = *static_cast<Access *>(owner);
            *value.error = sync_cpu(value.fd, DMA_BUF_SYNC_END | DMA_BUF_SYNC_READ);
        });
        image = unpack_linear_frame(frame, {static_cast<const unsigned char *>(mapped), size});
    }
    if (end_error)
        error("snapshot.sync_end", end_error);
    return image;
}

void save_png(const CpuImage &image, const std::string &path) {
#ifdef CLOUDPLAY_HAVE_PNG
    png_image png{};
    png.version = PNG_IMAGE_VERSION;
    png.width = image.width;
    png.height = image.height;
    png.format = PNG_FORMAT_RGB;
    if (image.rgb.size() != PNG_IMAGE_SIZE(png))
        error("snapshot.png_size");
    const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (fd < 0)
        error("snapshot.create_png", errno);
    auto *file = fdopen(fd, "wb");
    if (!file) {
        const int code = errno;
        close(fd);
        unlink(path.c_str());
        error("snapshot.open_png", code);
    }
    const bool written = png_image_write_to_stdio(&png, file, 0, image.rgb.data(), 0, nullptr) != 0;
    png_image_free(&png);
    const bool closed = fclose(file) == 0;
    if (!written || !closed) {
        unlink(path.c_str());
        error("snapshot.write_png");
    }
#else
    (void)image;
    (void)path;
    error("snapshot.libpng_development_dependency_missing");
#endif
}

CpuImage load_png(const std::string &path) {
#ifdef CLOUDPLAY_HAVE_PNG
    png_image png{};
    png.version = PNG_IMAGE_VERSION;
    struct Release {
        png_image *image;
        ~Release() { png_image_free(image); }
    } release{&png};
    if (!png_image_begin_read_from_file(&png, path.c_str()) || png.width == 0 || png.height == 0 ||
        png.width > 8192 || png.height > 8192)
        error("snapshot.reference_png_header");
    png.format = PNG_FORMAT_RGB;
    CpuImage image{png.width, png.height, std::vector<unsigned char>(PNG_IMAGE_SIZE(png))};
    if (!png_image_finish_read(&png, nullptr, image.rgb.data(), 0, nullptr))
        error("snapshot.reference_png_read");
    return image;
#else
    (void)path;
    error("snapshot.libpng_development_dependency_missing");
#endif
}

void log_buffer_diagnostics(const FrameBufferDiagnostics &d) {
    const char *name = "unsupported";
    switch (d.spa_format) {
    case SPA_VIDEO_FORMAT_BGRx:
        name = "BGRx";
        break;
    case SPA_VIDEO_FORMAT_BGRA:
        name = "BGRA";
        break;
    case SPA_VIDEO_FORMAT_RGBx:
        name = "RGBx";
        break;
    case SPA_VIDEO_FORMAT_RGBA:
        name = "RGBA";
        break;
    default:
        break;
    }
    std::cout << "{\"event\":\"capture.buffer\",\"spaFormat\":" << d.spa_format
              << ",\"spaFormatName\":\"" << name
              << "\",\"requestedPath\":\"DMA-BUF -> NVIDIA EGLImage\",\"explicitSyncPresent\":"
              << (d.explicit_sync_present ? "true" : "false")
              << ",\"implicitFencesReady\":" << (d.implicit_fences_ready ? "true" : "false")
              << ",\"eglImageImported\":" << (d.egl_image_imported ? "true" : "false")
              << ",\"planeCount\":" << d.plane_count << ",\"planes\":[";
    for (std::uint32_t i = 0; i < std::min(d.plane_count, 4U); ++i) {
        if (i)
            std::cout << ',';
        const char *memory_name = d.memory_types[i] == SPA_DATA_DmaBuf   ? "DmaBuf"
                                  : d.memory_types[i] == SPA_DATA_MemFd  ? "MemFd"
                                  : d.memory_types[i] == SPA_DATA_MemPtr ? "MemPtr"
                                                                         : "unsupported";
        std::cout << "{\"memoryType\":" << d.memory_types[i] << ",\"memoryTypeName\":\""
                  << memory_name << '"' << ",\"dataFlags\":" << d.data_flags[i]
                  << ",\"isDmaBuf\":" << (d.memory_types[i] == SPA_DATA_DmaBuf ? "true" : "false")
                  << ",\"mapOffset\":" << d.map_offsets[i]
                  << ",\"chunkOffset\":" << d.chunk_offsets[i] << ",\"effectiveOffset\":"
                  << static_cast<std::uint64_t>(d.map_offsets[i]) +
                         (d.max_sizes[i] ? d.chunk_offsets[i] % d.max_sizes[i] : d.chunk_offsets[i])
                  << ",\"chunkSize\":" << d.chunk_sizes[i] << ",\"maxSize\":" << d.max_sizes[i]
                  << ",\"stride\":" << d.strides[i] << '}';
    }
    std::cout << "],\"cropPresent\":" << (d.crop_present ? "true" : "false") << ",\"crop\":["
              << d.crop_x << ',' << d.crop_y << ',' << d.crop_width << ',' << d.crop_height
              << "],\"transformPresent\":" << (d.transform_present ? "true" : "false")
              << ",\"transform\":" << d.transform << "}\n";
}
} // namespace cloudplay::capture

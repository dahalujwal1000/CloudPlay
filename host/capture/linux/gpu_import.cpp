#include "gpu_import.hpp"
#include <array>
#include <cstring>
#include <drm_fourcc.h>
#include <string_view>

namespace cloudplay::capture {
namespace {
template <typename Function> Function load(const char *name) {
    const auto pointer = eglGetProcAddress(name);
    Function function{};
    static_assert(sizeof(function) == sizeof(pointer));
    std::memcpy(&function, &pointer, sizeof(function));
    return function;
}
bool extension(const char *list, std::string_view name) {
    if (!list)
        return false;
    std::string_view remaining(list);
    while (!remaining.empty()) {
        const auto end = remaining.find(' ');
        if (remaining.substr(0, end) == name)
            return true;
        if (end == std::string_view::npos)
            break;
        remaining.remove_prefix(end + 1);
    }
    return false;
}
} // namespace

GpuImporter::GpuImporter() {
    const auto devices = load<PFNEGLQUERYDEVICESEXTPROC>("eglQueryDevicesEXT");
    const auto device_string = load<PFNEGLQUERYDEVICESTRINGEXTPROC>("eglQueryDeviceStringEXT");
    const auto device_attr = load<PFNEGLQUERYDEVICEATTRIBEXTPROC>("eglQueryDeviceAttribEXT");
    const auto platform = load<PFNEGLGETPLATFORMDISPLAYEXTPROC>("eglGetPlatformDisplayEXT");
    query_ = load<PFNEGLQUERYDMABUFMODIFIERSEXTPROC>("eglQueryDmaBufModifiersEXT");
    create_ = load<PFNEGLCREATEIMAGEKHRPROC>("eglCreateImageKHR");
    destroy_ = load<PFNEGLDESTROYIMAGEKHRPROC>("eglDestroyImageKHR");
    if (!devices || !device_string || !device_attr || !platform || !query_ || !create_ || !destroy_)
        throw FrameCaptureError(FrameCaptureFailure::GpuImport);
    std::array<EGLDeviceEXT, 16> available{};
    EGLint count{};
    if (!devices(static_cast<EGLint>(available.size()), available.data(), &count))
        throw FrameCaptureError(FrameCaptureFailure::GpuImport);
    for (int i = 0; i < count; ++i) {
        if (!extension(device_string(available[i], EGL_EXTENSIONS), "EGL_NV_device_cuda"))
            continue;
        EGLAttrib cuda{};
        if (!device_attr(available[i], EGL_CUDA_DEVICE_NV, &cuda) || cuda != 0)
            continue;
        display_ = platform(EGL_PLATFORM_DEVICE_EXT, available[i], nullptr);
        break;
    }
    if (display_ == EGL_NO_DISPLAY || !eglInitialize(display_, nullptr, nullptr)) {
        display_ = EGL_NO_DISPLAY;
        throw FrameCaptureError(FrameCaptureFailure::GpuImport);
    }
    if (!extension(eglQueryString(display_, EGL_EXTENSIONS), "EGL_EXT_image_dma_buf_import_modifiers")) {
        eglTerminate(display_);
        display_ = EGL_NO_DISPLAY;
        throw FrameCaptureError(FrameCaptureFailure::GpuImport);
    }
}

GpuImporter::~GpuImporter() {
    if (display_ != EGL_NO_DISPLAY)
        eglTerminate(display_);
}

std::vector<std::uint64_t> GpuImporter::modifiers(std::uint32_t format) const {
    EGLint count{};
    if (!query_(display_, static_cast<EGLint>(format), 0, nullptr, nullptr, &count) || count < 1 || count > 256)
        return {};
    std::vector<EGLuint64KHR> values(static_cast<std::size_t>(count));
    if (!query_(display_, static_cast<EGLint>(format), count, values.data(), nullptr, &count))
        return {};
    values.resize(static_cast<std::size_t>(count));
    return {values.begin(), values.end()};
}

EGLImageKHR GpuImporter::import(const CapturedFrame &frame) const {
    constexpr std::array<EGLint, 4> fds{EGL_DMA_BUF_PLANE0_FD_EXT, EGL_DMA_BUF_PLANE1_FD_EXT, EGL_DMA_BUF_PLANE2_FD_EXT, EGL_DMA_BUF_PLANE3_FD_EXT};
    constexpr std::array<EGLint, 4> offsets{EGL_DMA_BUF_PLANE0_OFFSET_EXT, EGL_DMA_BUF_PLANE1_OFFSET_EXT, EGL_DMA_BUF_PLANE2_OFFSET_EXT, EGL_DMA_BUF_PLANE3_OFFSET_EXT};
    constexpr std::array<EGLint, 4> pitches{EGL_DMA_BUF_PLANE0_PITCH_EXT, EGL_DMA_BUF_PLANE1_PITCH_EXT, EGL_DMA_BUF_PLANE2_PITCH_EXT, EGL_DMA_BUF_PLANE3_PITCH_EXT};
    constexpr std::array<EGLint, 4> lo{EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT, EGL_DMA_BUF_PLANE1_MODIFIER_LO_EXT, EGL_DMA_BUF_PLANE2_MODIFIER_LO_EXT, EGL_DMA_BUF_PLANE3_MODIFIER_LO_EXT};
    constexpr std::array<EGLint, 4> hi{EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT, EGL_DMA_BUF_PLANE1_MODIFIER_HI_EXT, EGL_DMA_BUF_PLANE2_MODIFIER_HI_EXT, EGL_DMA_BUF_PLANE3_MODIFIER_HI_EXT};
    std::vector<EGLint> attributes{EGL_WIDTH, static_cast<EGLint>(frame.width), EGL_HEIGHT,
        static_cast<EGLint>(frame.height), EGL_LINUX_DRM_FOURCC_EXT, static_cast<EGLint>(frame.drm_format)};
    for (std::uint32_t i = 0; i < frame.plane_count; ++i) {
        const auto &plane = frame.planes[i];
        attributes.insert(attributes.end(), {fds[i], plane.fd, offsets[i], static_cast<EGLint>(plane.offset), pitches[i], plane.stride});
        if (frame.modifier != DRM_FORMAT_MOD_INVALID)
            attributes.insert(attributes.end(), {lo[i], static_cast<EGLint>(frame.modifier & 0xffffffffU), hi[i], static_cast<EGLint>(frame.modifier >> 32)});
    }
    attributes.push_back(EGL_NONE);
    return create_(display_, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, nullptr, attributes.data());
}

void GpuImporter::release(EGLImageKHR image) const noexcept {
    destroy_(display_, image);
}
} // namespace cloudplay::capture

#include "../linux/cpu_snapshot.hpp"
#include <array>
#include <drm_fourcc.h>
#include <filesystem>
#include <stdexcept>
#include <unistd.h>

int main() {
    using namespace cloudplay::capture;
    CapturedFrame frame;
    frame.width = frame.height = 2;
    frame.plane_count = 1;
    frame.modifier = DRM_FORMAT_MOD_LINEAR;
    frame.drm_format = DRM_FORMAT_XRGB8888;
    frame.planes[0] = {-1, 4, 12};
    // Offset and row padding must never become pixels; asymmetric rows detect flips.
    const std::array<unsigned char, 24> bytes{99, 99, 99, 99, 0,   0, 255, 7, 0,   255, 0,   8,
                                              99, 99, 99, 99, 255, 0, 0,   9, 255, 255, 255, 10};
    const auto image = unpack_linear_frame(frame, bytes);
    const std::vector<unsigned char> expected{255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255};
    if (image.rgb != expected)
        throw std::runtime_error("Stride, offset, channels or orientation incorrect");
    frame.drm_format = DRM_FORMAT_ABGR8888;
    if (unpack_linear_frame(frame, bytes).rgb[0] != 0 ||
        unpack_linear_frame(frame, bytes).rgb[2] != 255)
        throw std::runtime_error("RGB bytes misinterpreted as BGR");
    for (unsigned condition = 0; condition < 6; ++condition) {
        auto bad = frame;
        if (condition == 0)
            bad.modifier = 1;
        if (condition == 1)
            bad.planes[0].stride = 4;
        if (condition == 2)
            bad.planes[0].offset = 24;
        if (condition == 3)
            bad.plane_count = 2;
        if (condition == 4)
            bad.diagnostics.transform = 1;
        if (condition == 5) {
            bad.diagnostics.crop_present = true;
            bad.diagnostics.crop_width = bad.diagnostics.crop_height = 1;
        }
        bool rejected{};
        try {
            (void)unpack_linear_frame(bad, bytes);
        } catch (const FrameCaptureError &) {
            rejected = true;
        }
        if (!rejected)
            throw std::runtime_error("Unsupported layout accepted");
    }
    bool unsynchronized{};
    try {
        (void)read_cpu_snapshot(frame);
    } catch (const FrameCaptureError &e) {
        unsynchronized = e.operation == "snapshot.fence_not_verified";
    }
    if (!unsynchronized)
        throw std::runtime_error("Read started without verified fence");
#ifdef CLOUDPLAY_HAVE_PNG
    const auto path = std::filesystem::temp_directory_path() /
                      ("cloudplay-png-test-" + std::to_string(getpid()) + ".png");
    struct Remove {
        std::filesystem::path path;
        ~Remove() {
            std::error_code error;
            std::filesystem::remove(path, error);
        }
    } remove{path};
    save_png(image, path.string());
    const auto loaded = load_png(path.string());
    if (loaded.width != 2 || loaded.height != 2 || loaded.rgb != expected)
        throw std::runtime_error("PNG round trip altered pixels");
    bool overwrite{};
    try {
        save_png(image, path.string());
    } catch (const FrameCaptureError &) {
        overwrite = true;
    }
    if (!overwrite)
        throw std::runtime_error("PNG overwrote existing file");
#endif
}

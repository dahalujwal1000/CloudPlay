#pragma once

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <cloudplay/capture/frame_capture.hpp>
#include <vector>

namespace cloudplay::capture {
class GpuImporter final {
  public:
    GpuImporter();
    ~GpuImporter();
    GpuImporter(const GpuImporter &) = delete;
    GpuImporter &operator=(const GpuImporter &) = delete;
    [[nodiscard]] std::vector<std::uint64_t> modifiers(std::uint32_t format) const;
    [[nodiscard]] EGLImageKHR import(const CapturedFrame &frame) const;
    void release(EGLImageKHR image) const noexcept;

  private:
    EGLDisplay display_{EGL_NO_DISPLAY};
    PFNEGLQUERYDMABUFMODIFIERSEXTPROC query_{};
    PFNEGLCREATEIMAGEKHRPROC create_{};
    PFNEGLDESTROYIMAGEKHRPROC destroy_{};
};
} // namespace cloudplay::capture

#pragma once

#include <cloudplay/capture/frame_policy.hpp>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <windows.h>

struct ID3D11Texture2D;

namespace cloudplay::capture {

enum class CaptureState { Idle, Starting, Capturing, Delivering, Stopping, Stopped, Failed };
enum class CaptureFailure {
    InvalidTarget,
    Unsupported,
    TargetClosed,
    DeviceLost,
    Platform,
    PermissionDenied
};

class CaptureError : public std::runtime_error {
  public:
    CaptureError(CaptureFailure failure, HRESULT code)
        : std::runtime_error("Windows capture failed"), reason(failure), result(code) {}
    CaptureFailure reason;
    HRESULT result;
};

struct FrameView {
    // Borrowed GPU texture. Valid only until the synchronous consumer returns.
    ID3D11Texture2D *texture;
    FrameSize content_size;
    std::int64_t timestamp_100ns;
    double capture_latency_us;
};

struct CaptureStats {
    std::uint64_t received{};
    std::uint64_t delivered{};
    std::uint64_t discarded{};
    std::uint64_t resizes{};
    double last_capture_latency_us{};
    std::optional<CaptureFailure> failure;
    HRESULT result{S_OK};
};

// One apartment-initialized owner thread must construct/use/destroy this object.
// Consumers must finish GPU use (or copy to owned GPU storage) before returning.
// Consumers must not reenter or destroy capture. No frame/texture references may escape.
class WindowCapture {
  public:
    WindowCapture();
    ~WindowCapture();
    WindowCapture(const WindowCapture &) = delete;
    WindowCapture &operator=(const WindowCapture &) = delete;

    void start(HWND window);
    bool poll(const std::function<void(const FrameView &)> &consumer);
    void stop();
    [[nodiscard]] CaptureState state() const;
    [[nodiscard]] CaptureStats stats() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace cloudplay::capture

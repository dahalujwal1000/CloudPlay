#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace cloudplay::capture {

enum class FrameCaptureState { Idle, Starting, Capturing, Delivering, Stopping, Stopped, Failed };
enum class FrameCaptureFailure {
    SessionUnavailable,
    PermissionDenied,
    Timeout,
    Portal,
    PipeWire,
    UnsupportedFormat,
    GpuImport,
    Consumer
};
enum class FrameStorage { DmaBuf, D3D11 };

struct CaptureOptions {
    std::uint32_t width{1920};
    std::uint32_t height{1080};
    std::uint32_t fps{60};
};

struct DmaBufPlane {
    int fd{-1};
    std::uint32_t offset{};
    std::int32_t stride{};
};

struct CapturedFrame {
    FrameStorage storage{FrameStorage::DmaBuf};
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t drm_format{};
    std::uint64_t modifier{};
    std::array<DmaBufPlane, 4> planes{};
    std::uint32_t plane_count{};
    std::int64_t timestamp_ns{};
    void *native_image{}; // Borrowed EGLImage on Linux; not an NVENC input handle.
};

struct FrameCaptureMetrics {
    std::uint64_t received{};
    std::uint64_t delivered{};
    std::uint64_t discarded{};
    std::uint64_t sequence_gaps{};
    std::uint64_t gpu_imports{};
    std::uint64_t cpu_frames{};
    std::uint64_t latency_samples{};
    double latency_sum_ms{};
    double max_latency_ms{};
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t drm_format{};
    std::uint64_t modifier{};
    // Capture-module copies only. Compositor/driver internal copies are unknown.
    std::uint64_t cpu_copies{};
    std::uint64_t gpu_copies{};
};

class FrameCaptureError : public std::runtime_error {
  public:
    explicit FrameCaptureError(FrameCaptureFailure failure, std::string_view stage = "capture", int code = 0)
        : std::runtime_error("Frame capture failed"), reason(failure), operation(stage), native_code(code) {}
    FrameCaptureFailure reason;
    std::string operation;
    int native_code;
};

// Owner-thread API. Frames/images/fds are borrowed only during consume().
// Complete GPU work before returning. Do not reenter/destroy capture from consume().
class IFrameCapture {
  public:
    using Consumer = std::function<void(const CapturedFrame &)>;
    virtual ~IFrameCapture() = default;
    virtual void start(const CaptureOptions &options) = 0;
    virtual bool poll(const Consumer &consume) = 0;
    virtual void stop() = 0;
    [[nodiscard]] virtual FrameCaptureState state() const = 0;
    [[nodiscard]] virtual FrameCaptureMetrics metrics() const = 0;
};

// Ensures a dequeued native buffer is returned exactly once, including exceptions.
class FrameLease final {
  public:
    using Release = void (*)(void *, void *) noexcept;
    FrameLease(void *owner, void *buffer, Release release) noexcept
        : owner_(owner), buffer_(buffer), release_(release) {}
    ~FrameLease() { release_(owner_, buffer_); }
    FrameLease(const FrameLease &) = delete;
    FrameLease &operator=(const FrameLease &) = delete;
    FrameLease(FrameLease &&) = delete;
    FrameLease &operator=(FrameLease &&) = delete;

  private:
    void *owner_;
    void *buffer_;
    Release release_;
};

} // namespace cloudplay::capture

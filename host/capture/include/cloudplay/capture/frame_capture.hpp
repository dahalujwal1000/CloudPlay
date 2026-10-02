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
enum class FrameStorage { DmaBuf, D3D11, CpuMemory };

struct CaptureOptions {
    std::uint32_t width{1920};
    std::uint32_t height{1080};
    std::uint32_t fps{60};
    bool capture_diagnostics{};
    bool cpu_capture{}; // Linux diagnostic path only; never performance acceptance.
};

struct DmaBufPlane {
    int fd{-1};
    std::uint32_t offset{};
    std::int32_t stride{};
};

struct FrameBufferDiagnostics {
    std::uint32_t spa_format{};
    std::uint32_t plane_count{};
    std::array<std::uint32_t, 4> memory_types{}, data_flags{}, map_offsets{}, chunk_offsets{},
        chunk_sizes{}, max_sizes{};
    std::array<std::int32_t, 4> strides{};
    bool crop_present{};
    std::int32_t crop_x{}, crop_y{};
    std::uint32_t crop_width{}, crop_height{};
    bool transform_present{};
    std::uint32_t transform{};
    bool explicit_sync_present{};
    bool implicit_fences_ready{};
    bool egl_image_imported{};
    std::array<std::int64_t, 4> fds{-1, -1, -1, -1}; // Diagnostic values, not owned handles.
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
    FrameBufferDiagnostics diagnostics;
    const unsigned char *cpu_data{}; // Borrowed mapped plane, valid only in consume().
    std::size_t cpu_size{};
};

struct FrameCaptureMetrics {
    FrameBufferDiagnostics buffer;
    std::uint64_t received{};
    std::uint64_t released{};
    std::uint64_t delivered{};
    std::uint64_t discarded{};
    std::uint64_t sequence_gaps{};
    std::uint64_t gpu_imports{};
    std::uint64_t cpu_frames{};
    std::uint64_t latency_samples{};
    double latency_sum_ms{};
    double max_latency_ms{};
    std::uint64_t presentation_age_samples{};
    std::uint64_t future_timestamps{};
    double presentation_age_sum_ms{};
    double minimum_presentation_age_ms{};
    std::uint64_t presentation_intervals{};
    double presentation_interval_sum_ms{};
    double max_presentation_interval_ms{};
    double gpu_import_time_sum_ms{};
    double max_gpu_import_time_ms{};
    double negotiated_fps{};
    double negotiated_max_fps{};
    int native_error{};
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
    explicit FrameCaptureError(FrameCaptureFailure failure, std::string_view stage = "capture",
                               int code = 0)
        : std::runtime_error("Frame capture failed"), reason(failure), operation(stage),
          native_code(code) {}
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

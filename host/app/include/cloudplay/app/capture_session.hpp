#pragma once

#include <cloudplay/capture/frame_capture.hpp>
#include <cstdint>
#include <exception>
#include <memory>

namespace cloudplay::app {

enum class CaptureSessionState {
    Idle,
    Starting,
    Capturing,
    Delivering,
    Stopping,
    Stopped,
    Failed,
    CleanupFailed
};

struct CaptureSessionDiagnostics {
    std::uint64_t start_attempts{};
    std::uint64_t failures{};
    std::uint64_t stop_failures{};
    std::uint64_t delivered_frames{};
};

// Owner-thread orchestration. No automatic retries or retained borrowed frames.
// Consumers must not reenter or destroy this session, and must complete GPU work.
class CaptureSession final {
  public:
    explicit CaptureSession(std::unique_ptr<capture::IFrameCapture> backend);
    ~CaptureSession();
    CaptureSession(const CaptureSession &) = delete;
    CaptureSession &operator=(const CaptureSession &) = delete;
    CaptureSession(CaptureSession &&) = delete;
    CaptureSession &operator=(CaptureSession &&) = delete;

    void start(const capture::CaptureOptions &options);
    bool poll(const capture::IFrameCapture::Consumer &consume);
    void stop();
    [[nodiscard]] CaptureSessionState state() const noexcept { return state_; }
    [[nodiscard]] CaptureSessionDiagnostics diagnostics() const noexcept { return diagnostics_; }
    [[nodiscard]] std::exception_ptr last_error() const noexcept { return last_error_; }
    [[nodiscard]] std::exception_ptr cleanup_error() const noexcept { return cleanup_error_; }
    [[nodiscard]] capture::FrameCaptureMetrics capture_metrics() const;

  private:
    void cleanup() noexcept;
    void fail() noexcept;
    void adopt_backend_state();
    std::unique_ptr<capture::IFrameCapture> backend_;
    CaptureSessionState state_{CaptureSessionState::Idle};
    CaptureSessionDiagnostics diagnostics_;
    std::exception_ptr last_error_, cleanup_error_;
};

} // namespace cloudplay::app

#include <cloudplay/app/capture_session.hpp>
#include <stdexcept>
#include <utility>

namespace cloudplay::app {

CaptureSession::CaptureSession(std::unique_ptr<capture::IFrameCapture> backend)
    : backend_(std::move(backend)) {
    if (!backend_)
        throw std::invalid_argument("Capture backend is required");
}

CaptureSession::~CaptureSession() {
    if (state_ != CaptureSessionState::Idle && state_ != CaptureSessionState::Stopped &&
        state_ != CaptureSessionState::Failed)
        cleanup();
}

capture::FrameCaptureMetrics CaptureSession::capture_metrics() const { return backend_->metrics(); }

void CaptureSession::cleanup() noexcept {
    state_ = CaptureSessionState::Stopping;
    try {
        backend_->stop();
        cleanup_error_ = nullptr;
        state_ = CaptureSessionState::Stopped;
    } catch (...) {
        cleanup_error_ = std::current_exception();
        ++diagnostics_.stop_failures;
        state_ = CaptureSessionState::CleanupFailed;
    }
}

void CaptureSession::fail() noexcept {
    last_error_ = std::current_exception();
    ++diagnostics_.failures;
    cleanup();
    if (state_ == CaptureSessionState::Stopped)
        state_ = CaptureSessionState::Failed;
}

void CaptureSession::adopt_backend_state() {
    const auto backend_state = backend_->state();
    if (backend_state == capture::FrameCaptureState::Starting)
        state_ = CaptureSessionState::Starting;
    else if (backend_state == capture::FrameCaptureState::Capturing)
        state_ = CaptureSessionState::Capturing;
    else
        throw std::runtime_error("Capture backend is not running");
}

void CaptureSession::start(const capture::CaptureOptions &options) {
    if (state_ != CaptureSessionState::Idle && state_ != CaptureSessionState::Stopped &&
        state_ != CaptureSessionState::Failed)
        throw std::logic_error("Capture start requires an inactive, cleaned session");
    state_ = CaptureSessionState::Starting;
    last_error_ = cleanup_error_ = nullptr;
    ++diagnostics_.start_attempts;
    try {
        backend_->start(options);
        adopt_backend_state();
    } catch (...) {
        fail();
        throw;
    }
}

bool CaptureSession::poll(const capture::IFrameCapture::Consumer &consume) {
    if (state_ != CaptureSessionState::Starting && state_ != CaptureSessionState::Capturing)
        throw std::logic_error("Capture poll requires an active session");
    if (!consume)
        throw std::invalid_argument("Capture consumer is required");
    try {
        const bool delivered = backend_->poll([&](const capture::CapturedFrame &frame) {
            state_ = CaptureSessionState::Delivering;
            try {
                consume(frame);
                ++diagnostics_.delivered_frames;
                state_ = CaptureSessionState::Capturing;
            } catch (...) {
                state_ = CaptureSessionState::Capturing;
                throw;
            }
        });
        adopt_backend_state();
        return delivered;
    } catch (...) {
        // The backend poll stack has unwound, so all borrowed frames are released.
        fail();
        throw;
    }
}

void CaptureSession::stop() {
    if (state_ == CaptureSessionState::Delivering || state_ == CaptureSessionState::Stopping)
        throw std::logic_error("Capture lifecycle cannot be reentered");
    if (state_ == CaptureSessionState::Idle || state_ == CaptureSessionState::Stopped ||
        state_ == CaptureSessionState::Failed) {
        state_ = CaptureSessionState::Stopped;
        return;
    }
    cleanup();
    if (cleanup_error_)
        std::rethrow_exception(cleanup_error_);
}

} // namespace cloudplay::app

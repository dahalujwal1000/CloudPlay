#include <cloudplay/capture/window_capture.hpp>
#include <stdexcept>
#include <thread>

int main() {
    using namespace cloudplay::capture;
    WindowCapture capture;
    if (capture.state() != CaptureState::Idle)
        return 1;
    try {
        capture.start(nullptr);
        return 1;
    } catch (const CaptureError &error) {
        if (error.reason != CaptureFailure::InvalidTarget || error.result != E_INVALIDARG)
            return 1;
    }
    if (capture.state() != CaptureState::Failed || !capture.stats().failure)
        return 1;
    capture.stop();
    capture.stop();
    if (capture.state() != CaptureState::Stopped)
        return 1;
    bool wrong_thread_rejected = false;
    std::thread worker([&] {
        try {
            capture.stop();
        } catch (const std::logic_error &) {
            wrong_thread_rejected = true;
        }
    });
    worker.join();
    return wrong_thread_rejected ? 0 : 1;
}

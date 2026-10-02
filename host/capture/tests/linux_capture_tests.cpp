#include <cloudplay/capture/pipewire_capture.hpp>
#include <stdexcept>
#include <thread>

int main() {
    using namespace cloudplay::capture;
    PipeWireCapture capture;
    IFrameCapture &interface = capture;
    if (interface.state() != FrameCaptureState::Idle)
        throw std::runtime_error("Invalid initial state");
    bool wrong_thread_rejected{};
    std::thread other([&] {
        try {
            interface.stop();
        } catch (const std::logic_error &) {
            wrong_thread_rejected = true;
        }
    });
    other.join();
    if (!wrong_thread_rejected)
        throw std::runtime_error("Cross-thread shutdown permitted");
    try {
        interface.start({1280, 720, 60});
        throw std::runtime_error("Invalid target accepted");
    } catch (const std::invalid_argument &) {
    }
    for (int i = 0; i < 3; ++i) {
        try {
            interface.start({});
            throw std::runtime_error("Unavailable session accepted");
        } catch (const FrameCaptureError &error) {
            if (error.reason != FrameCaptureFailure::SessionUnavailable ||
                interface.state() != FrameCaptureState::Failed)
                throw std::runtime_error("Wrong startup failure");
        }
        interface.stop();
        interface.stop();
        if (interface.state() != FrameCaptureState::Stopped || interface.metrics().delivered != 0)
            throw std::runtime_error("Shutdown not clean/idempotent");
    }
}

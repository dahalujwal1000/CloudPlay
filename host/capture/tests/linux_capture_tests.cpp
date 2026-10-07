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
    bool timing_thread_rejected{};
    std::thread other([&] {
        try {
            interface.stop();
        } catch (const std::logic_error &) {
            wrong_thread_rejected = true;
        }
        try {
            (void)capture.timing_summary();
        } catch (const std::logic_error &) {
            timing_thread_rejected = true;
        }
    });
    other.join();
    if (!wrong_thread_rejected || !timing_thread_rejected)
        throw std::runtime_error("Cross-thread shutdown/timing access permitted");
    try {
        interface.start({1280, 720, 60});
        throw std::runtime_error("Invalid target accepted");
    } catch (const std::invalid_argument &) {
    }
    try {
        CaptureOptions invalid;
        invalid.source = static_cast<CaptureSource>(0);
        interface.start(invalid);
        throw std::runtime_error("Invalid source accepted");
    } catch (const std::invalid_argument &) {
    }
    for (int i = 0; i < 3; ++i) {
        try {
            CaptureOptions invalid;
            invalid.diagnostic_max_fps = i == 0 ? 59 : i == 1 ? 66 : 61;
            invalid.fixed_rate = i == 2;
            interface.start(invalid);
            throw std::runtime_error("Invalid diagnostic ceiling accepted");
        } catch (const std::invalid_argument &) {
        }
        try {
            CaptureOptions invalid;
            invalid.buffer_pool_size = i == 0 ? 0 : i == 1 ? 1 : 9;
            interface.start(invalid);
            throw std::runtime_error("Invalid buffer pool accepted");
        } catch (const std::invalid_argument &) {
        }
        CaptureOptions options;
        options.timing_diagnostics = i != 1;
        options.fixed_rate = i == 2;
        options.buffer_pool_size = i == 2 ? 8 : 4;
        try {
            interface.start(options);
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
        if (interface.metrics().requested_buffer_pool_size ||
            interface.metrics().max_outstanding_buffers || interface.metrics().dequeue_batches)
            throw std::runtime_error("Failed startup reported live buffer pressure");
        for (const auto &timing : capture.timing_summary())
            if (timing.count || timing.overflow)
                throw std::runtime_error("Failed startup/restart retained timing data");
    }
}

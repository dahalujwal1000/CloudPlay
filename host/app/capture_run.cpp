#include <cloudplay/app/capture_run.hpp>
#include <cloudplay/app/capture_session.hpp>
#include <optional>
#include <stdexcept>
#include <thread>

namespace cloudplay::app {
namespace {
const char *state_name(CaptureSessionState state) noexcept {
    switch (state) {
    case CaptureSessionState::Idle:
        return "IDLE";
    case CaptureSessionState::Starting:
        return "STARTING";
    case CaptureSessionState::Capturing:
        return "CAPTURING";
    case CaptureSessionState::Delivering:
        return "DELIVERING";
    case CaptureSessionState::Stopping:
        return "STOPPING";
    case CaptureSessionState::Stopped:
        return "STOPPED";
    case CaptureSessionState::Failed:
        return "FAILED";
    case CaptureSessionState::CleanupFailed:
        return "CLEANUP_FAILED";
    }
    return "UNKNOWN";
}
void state_event(std::ostream &output, CaptureSessionState state) {
    output << "{\"component\":\"Host.App\",\"event\":\"host.capture_state\",\"state\":\""
           << state_name(state) << "\"}\n"
           << std::flush;
}
} // namespace

static int capture_cycle(CaptureSession &session, std::chrono::milliseconds duration,
                         std::ostream &output, const std::function<bool()> &cancel) {
    using Clock = std::chrono::steady_clock;
    std::optional<Clock::time_point> first_frame;
    int result{};
    try {
        if (cancel())
            result = 130;
        else {
            capture::CaptureOptions options;
            options.source = capture::CaptureSource::Monitor;
            session.start(options);
            state_event(output, session.state());
            auto reported_state = session.state();
            const auto first_frame_deadline = Clock::now() + std::chrono::seconds(10);
            while (true) {
                if (cancel()) {
                    result = 130;
                    break;
                }
                const auto now = Clock::now();
                if (first_frame && now - *first_frame >= duration)
                    break;
                if (!first_frame && now >= first_frame_deadline)
                    throw capture::FrameCaptureError(capture::FrameCaptureFailure::Timeout,
                                                     "host.capture_first_frame");
                session.poll([&](const capture::CapturedFrame &) {
                    if (!first_frame)
                        first_frame = Clock::now();
                });
                if (session.state() != reported_state) {
                    state_event(output, session.state());
                    reported_state = session.state();
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        session.stop();
    } catch (const capture::FrameCaptureError &error) {
        output << "{\"component\":\"Host.App\",\"event\":\"host.capture_failed\",\"reason\":"
               << static_cast<int>(error.reason) << ",\"nativeCode\":" << error.native_code
               << "}\n";
        result = 1;
    } catch (...) {
        // Do not log arbitrary exception text that may contain sensitive data.
        output << "{\"component\":\"Host.App\",\"event\":\"host.capture_failed\",\"reason\":"
                  "\"unexpected\"}\n";
        result = 1;
    }
    if (session.state() == CaptureSessionState::Starting ||
        session.state() == CaptureSessionState::Capturing) {
        try {
            session.stop();
        } catch (...) {
            result = 1;
        }
    }
    state_event(output, session.state());
    const auto counts = session.diagnostics();
    const auto metrics = session.capture_metrics();
    const bool returned_all = metrics.received == metrics.released;
    if (!returned_all) {
        output << "{\"component\":\"Host.App\",\"event\":\"host.capture_failed\","
                  "\"reason\":\"buffer_return_mismatch\"}\n";
        result = 1;
    }
    const double elapsed =
        first_frame ? std::chrono::duration<double>(Clock::now() - *first_frame).count() : 0;
    output << "{\"component\":\"Host.App\",\"event\":\"host.capture_summary\",\"exitCode\":"
           << result << ",\"elapsedSeconds\":" << elapsed << ",\"received\":" << metrics.received
           << ",\"released\":" << metrics.released << ",\"delivered\":" << metrics.delivered
           << ",\"allBuffersReturned\":" << (returned_all ? "true" : "false")
           << ",\"gpuImports\":" << metrics.gpu_imports << ",\"cpuFrames\":" << metrics.cpu_frames
           << ",\"discarded\":" << metrics.discarded
           << ",\"sequenceGaps\":" << metrics.sequence_gaps << ",\"width\":" << metrics.width
           << ",\"height\":" << metrics.height << ",\"startAttempts\":" << counts.start_attempts
           << ",\"failures\":" << counts.failures << ",\"stopFailures\":" << counts.stop_failures
           << ",\"cleanupFailed\":"
           << (session.state() == CaptureSessionState::CleanupFailed ? "true" : "false")
           << ",\"performanceAcceptanceEvaluated\":false,\"nvencInteropVerified\":false}\n";
    return result;
}

int run_capture(std::unique_ptr<capture::IFrameCapture> backend, std::chrono::milliseconds duration,
                std::ostream &output, const std::function<bool()> &cancel,
                std::uint32_t requested_runs) {
    if (duration <= std::chrono::milliseconds::zero() || !cancel || requested_runs < 1 ||
        requested_runs > 3)
        throw std::invalid_argument(
            "Positive duration, cancellation callback and 1..3 runs required");
    CaptureSession session(std::move(backend));
    for (std::uint32_t run = 1; run <= requested_runs; ++run) {
        output << "{\"component\":\"Host.App\",\"event\":\"host.capture_run\",\"run\":" << run
               << ",\"requestedRuns\":" << requested_runs << "}\n"
               << std::flush;
        const int result = capture_cycle(session, duration, output, cancel);
        if (result != 0)
            return result; // Never reopen consent after denial, failure or cancellation.
    }
    return 0;
}

} // namespace cloudplay::app

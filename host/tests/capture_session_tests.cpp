#include <cloudplay/app/capture_run.hpp>
#include <cloudplay/app/capture_session.hpp>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <type_traits>

namespace {
using namespace cloudplay::capture;
using namespace cloudplay::app;

void check(bool condition) {
    if (!condition)
        throw std::runtime_error("Capture session test failed");
}
template <typename Error, typename Action> void rejects(Action action) {
    try {
        action();
    } catch (const Error &) {
        return;
    }
    throw std::runtime_error("Expected capture session exception");
}
struct Counts {
    int starts{}, stops{}, leases{}, returns{};
};
void release(void *owner, void *) noexcept {
    auto &counts = *static_cast<Counts *>(owner);
    --counts.leases;
    ++counts.returns;
}
class FakeCapture final : public IFrameCapture {
  public:
    explicit FakeCapture(Counts &counts) : counts_(counts) {}
    void start(const CaptureOptions &options) override {
        ++counts_.starts;
        check(options.width == 1920 && options.height == 1080);
        state_ = FrameCaptureState::Starting;
        if (deny)
            throw FrameCaptureError(FrameCaptureFailure::PermissionDenied, "test.permission", 7);
    }
    bool poll(const Consumer &consume) override {
        if (poll_failure)
            throw FrameCaptureError(FrameCaptureFailure::PipeWire, "test.disconnected");
        if (silent_failure) {
            state_ = FrameCaptureState::Failed;
            return false;
        }
        if (!emit)
            return false;
        ++counts_.leases;
        const FrameLease lease(&counts_, this, release);
        CapturedFrame frame;
        frame.width = 1920;
        frame.height = 1080;
        consume(frame);
        state_ = FrameCaptureState::Capturing;
        return true;
    }
    void stop() override {
        check(counts_.leases == 0);
        ++counts_.stops;
        if (stop_failure)
            throw std::runtime_error("test.cleanup");
        state_ = FrameCaptureState::Stopped;
    }
    FrameCaptureState state() const override { return state_; }
    FrameCaptureMetrics metrics() const override {
        FrameCaptureMetrics result;
        result.received = static_cast<std::uint64_t>(counts_.returns);
        result.released = result.received;
        if (return_mismatch)
            ++result.received;
        return result;
    }
    bool deny{}, emit{true}, poll_failure{}, silent_failure{}, stop_failure{}, return_mismatch{};

  private:
    Counts &counts_;
    FrameCaptureState state_{FrameCaptureState::Idle};
};
} // namespace

int main() {
    static_assert(!std::is_copy_constructible_v<CaptureSession>);
    static_assert(!std::is_move_constructible_v<CaptureSession>);
    rejects<std::invalid_argument>([] { CaptureSession invalid(nullptr); });
    const IFrameCapture::Consumer discard = [](const CapturedFrame &) {};
    Counts counts;
    auto backend = std::make_unique<FakeCapture>(counts);
    auto *fake = backend.get();
    CaptureSession session(std::move(backend));
    rejects<std::logic_error>([&] { session.poll(discard); });
    session.start({});
    check(session.state() == CaptureSessionState::Starting);
    rejects<std::logic_error>([&] { session.start({}); });
    rejects<std::invalid_argument>([&] { session.poll({}); });
    fake->emit = false;
    check(!session.poll(discard) && session.state() == CaptureSessionState::Starting);
    fake->emit = true;
    check(session.poll([&](const CapturedFrame &frame) {
        check(session.state() == CaptureSessionState::Delivering && frame.width == 1920);
        rejects<std::logic_error>([&] { session.stop(); });
        rejects<std::logic_error>([&] { session.start({}); });
        rejects<std::logic_error>([&] { session.poll(discard); });
    }));
    check(session.state() == CaptureSessionState::Capturing && counts.returns == 1);
    check(session.capture_metrics().received == 1 && session.capture_metrics().released == 1);
    session.stop();
    session.stop();
    check(counts.stops == 1 && session.state() == CaptureSessionState::Stopped);

    fake->deny = true;
    rejects<FrameCaptureError>([&] { session.start({}); });
    check(session.state() == CaptureSessionState::Failed && counts.stops == 2);
    try {
        std::rethrow_exception(session.last_error());
    } catch (const FrameCaptureError &error) {
        check(error.reason == FrameCaptureFailure::PermissionDenied && error.native_code == 7 &&
              error.operation == "test.permission");
    }
    fake->deny = false;
    session.start({});
    check(!session.last_error());
    rejects<std::runtime_error>([&] {
        session.poll([](const CapturedFrame &) { throw std::runtime_error("test.consumer"); });
    });
    check(session.state() == CaptureSessionState::Failed && counts.leases == 0 &&
          counts.returns == 2 && counts.stops == 3);
    session.start({});
    fake->poll_failure = true;
    fake->stop_failure = true;
    rejects<FrameCaptureError>([&] { session.poll(discard); });
    check(session.state() == CaptureSessionState::CleanupFailed && session.last_error() &&
          session.cleanup_error());
    rejects<std::logic_error>([&] { session.start({}); });
    rejects<std::runtime_error>([&] { session.stop(); });
    fake->stop_failure = false;
    session.stop();
    check(session.state() == CaptureSessionState::Stopped && !session.cleanup_error());
    fake->poll_failure = false;
    session.start({});
    fake->silent_failure = true;
    rejects<std::runtime_error>([&] { session.poll(discard); });
    check(session.state() == CaptureSessionState::Failed);
    check(session.diagnostics().start_attempts == 5 && session.diagnostics().failures == 4 &&
          session.diagnostics().stop_failures == 2 && session.diagnostics().delivered_frames == 1);

    Counts destruction;
    {
        CaptureSession pending(std::make_unique<FakeCapture>(destruction));
        pending.start({});
        pending.stop();
        check(pending.state() == CaptureSessionState::Stopped);
    }
    check(destruction.stops == 1);
    {
        CaptureSession active(std::make_unique<FakeCapture>(destruction));
        active.start({});
    }
    check(destruction.stops == 2);
    {
        auto broken = std::make_unique<FakeCapture>(destruction);
        broken->stop_failure = true;
        CaptureSession active(std::move(broken));
        active.start({});
    }
    check(destruction.stops == 3);

    Counts run_counts;
    std::ostringstream output;
    check(run_capture(std::make_unique<FakeCapture>(run_counts), std::chrono::milliseconds(2),
                      output, [] { return false; }) == 0);
    check(run_counts.starts == 1 && run_counts.stops == 1 && run_counts.returns > 0);
    check(output.str().find("\"performanceAcceptanceEvaluated\":false") != std::string::npos);
    check(output.str().find("\"state\":\"STOPPED\"") != std::string::npos);
    Counts cancelled;
    output.str("");
    check(run_capture(std::make_unique<FakeCapture>(cancelled), std::chrono::milliseconds(2),
                      output, [] { return true; }) == 130);
    check(cancelled.starts == 0 && cancelled.stops == 0);
    auto denied = std::make_unique<FakeCapture>(cancelled);
    denied->deny = true;
    output.str("");
    check(run_capture(std::move(denied), std::chrono::milliseconds(2), output,
                      [] { return false; }) == 1);
    check(cancelled.starts == 1 && cancelled.stops == 1);
    check(output.str().find("\"nativeCode\":7") != std::string::npos);
    Counts early_stop;
    int polls{};
    check(run_capture(std::make_unique<FakeCapture>(early_stop), std::chrono::seconds(1), output,
                      [&] { return ++polls == 3; }) == 130);
    check(early_stop.starts == 1 && early_stop.stops == 1 && early_stop.returns == 1);
    Counts leak_counts;
    auto mismatch = std::make_unique<FakeCapture>(leak_counts);
    mismatch->return_mismatch = true;
    output.str("");
    check(run_capture(std::move(mismatch), std::chrono::milliseconds(1), output,
                      [] { return false; }) == 1);
    check(output.str().find("buffer_return_mismatch") != std::string::npos);
    Counts cleanup_counts;
    auto cleanup_failure = std::make_unique<FakeCapture>(cleanup_counts);
    cleanup_failure->stop_failure = true;
    output.str("");
    check(run_capture(std::move(cleanup_failure), std::chrono::milliseconds(1), output,
                      [] { return false; }) == 1);
    check(output.str().find("\"cleanupFailed\":true") != std::string::npos);
    check(cleanup_counts.stops == 2);
    Counts restart_counts;
    output.str("");
    check(run_capture(
              std::make_unique<FakeCapture>(restart_counts), std::chrono::milliseconds(1), output,
              [] { return false; }, 2) == 0);
    check(restart_counts.starts == 2 && restart_counts.stops == 2);
    check(output.str().find("\"startAttempts\":2") != std::string::npos);
    Counts revoked_counts;
    auto revoked = std::make_unique<FakeCapture>(revoked_counts);
    revoked->poll_failure = true;
    output.str("");
    check(run_capture(
              std::move(revoked), std::chrono::milliseconds(1), output, [] { return false; }, 3) ==
          1);
    check(revoked_counts.starts == 1 && revoked_counts.stops == 1);
    check(output.str().find("\"run\":2") == std::string::npos);
    rejects<std::invalid_argument>([&] {
        run_capture(
            std::make_unique<FakeCapture>(restart_counts), std::chrono::milliseconds(1), output,
            [] { return false; }, 4);
    });
}

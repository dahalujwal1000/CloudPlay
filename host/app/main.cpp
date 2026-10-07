#include <charconv>
#include <chrono>
#include <csignal>
#ifdef CLOUDPLAY_HOST_LINUX_CAPTURE
#include <cloudplay/app/capture_run.hpp>
#include <cloudplay/capture/pipewire_capture.hpp>
#endif
#include <cloudplay/core/config.hpp>
#include <cloudplay/core/session.hpp>
#include <cloudplay/telemetry/logger.hpp>
#include <iostream>
#include <string_view>

namespace {
#ifdef CLOUDPLAY_HOST_LINUX_CAPTURE
volatile std::sig_atomic_t stop_requested{};
void request_stop(int) { stop_requested = 1; }
#endif
constexpr std::string_view usage = "Usage: cloudplay_host [--bitrate-mbps 6..20 | "
                                   "--capture-seconds 1..120 [--capture-runs 1..3]]\n";
} // namespace

int main(int argc, char **argv) {
    cloudplay::StreamConfig config;
    std::uint32_t capture_seconds{};
    std::uint32_t capture_runs{1};
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        std::cout << usage;
        return 0;
    }
    if (argc != 1) {
        if ((argc != 3 && argc != 5) || (std::string_view(argv[1]) != "--bitrate-mbps" &&
                                         std::string_view(argv[1]) != "--capture-seconds")) {
            std::cerr << usage;
            return 2;
        }
        const std::string_view value(argv[2]);
        const bool capture = std::string_view(argv[1]) == "--capture-seconds";
        if (argc == 5) {
            if (!capture || std::string_view(argv[3]) != "--capture-runs") {
                std::cerr << usage;
                return 2;
            }
            const std::string_view runs(argv[4]);
            const auto [end_runs, error_runs] =
                std::from_chars(runs.data(), runs.data() + runs.size(), capture_runs);
            if (error_runs != std::errc{} || end_runs != runs.data() + runs.size() ||
                capture_runs < 1 || capture_runs > 3) {
                std::cerr << "Capture runs must be 1..3\n";
                return 2;
            }
        }
        const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(),
                                                  capture ? capture_seconds : config.bitrate_mbps);
        if (error != std::errc{} || end != value.data() + value.size()) {
            std::cerr << "Invalid numeric option\n";
            return 2;
        }
        if (capture && (capture_seconds < 1 || capture_seconds > 120)) {
            std::cerr << "Capture duration must be 1..120 seconds\n";
            return 2;
        }
    }
    if (const auto error = cloudplay::validate(config)) {
        std::cerr << *error << '\n';
        return 2;
    }

    cloudplay::Session session;
    cloudplay::Logger logger(std::cout);
    int result{};
    for (const auto event : {cloudplay::SessionEvent::Start, cloudplay::SessionEvent::Initialized,
                             cloudplay::SessionEvent::Stop, cloudplay::SessionEvent::Stopped}) {
        if (event == cloudplay::SessionEvent::Stop && capture_seconds) {
#ifdef CLOUDPLAY_HOST_LINUX_CAPTURE
            const auto previous_int = std::signal(SIGINT, request_stop);
            const auto previous_term = std::signal(SIGTERM, request_stop);
            try {
                result = cloudplay::app::run_capture(
                    std::make_unique<cloudplay::capture::PipeWireCapture>(),
                    std::chrono::seconds(capture_seconds), std::cout,
                    [] { return stop_requested != 0; }, capture_runs);
            } catch (...) {
                std::cerr << "Host capture initialization failed\n";
                result = 1;
            }
            if (previous_int != SIG_ERR)
                std::signal(SIGINT, previous_int);
            if (previous_term != SIG_ERR)
                std::signal(SIGTERM, previous_term);
#else
            std::cerr << "Host capture requires a Linux build with CLOUDPLAY_LINUX_CAPTURE=ON\n";
            result = 2;
#endif
        }
        const auto actual_event = event == cloudplay::SessionEvent::Stop && result == 1
                                      ? cloudplay::SessionEvent::Fail
                                      : event;
        const auto transition =
            session.apply(actual_event, actual_event == cloudplay::SessionEvent::Fail
                                            ? std::optional{cloudplay::SessionFailure::Capture}
                                            : std::nullopt);
        if (!transition)
            return 1;
        logger.transition(*transition);
    }
    return result;
}

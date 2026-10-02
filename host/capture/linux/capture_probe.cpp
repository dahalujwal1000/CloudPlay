#include <charconv>
#include <chrono>
#include <cloudplay/capture/pipewire_capture.hpp>
#include <csignal>
#include <iostream>
#include <string_view>
#include <thread>

namespace {
volatile std::sig_atomic_t interrupted{};
void interrupt(int) { interrupted = 1; }
} // namespace

int main(int argc, char **argv) {
    unsigned seconds{30};
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        std::cout << "Usage: cloudplay_linux_capture_probe [--seconds 1..120]\n";
        return 0;
    }
    if (argc != 1) {
        if (argc != 3 || std::string_view(argv[1]) != "--seconds")
            return 2;
        const std::string_view duration(argv[2]);
        const auto [end, error] =
            std::from_chars(duration.data(), duration.data() + duration.size(), seconds);
        if (error != std::errc{} || end != duration.data() + duration.size() || seconds < 1 ||
            seconds > 120)
            return 2;
    }
    std::signal(SIGINT, interrupt);
    std::signal(SIGTERM, interrupt);
    cloudplay::capture::PipeWireCapture capture;
    try {
        std::cerr << "Select a 1920x1080 monitor or window in GNOME's sharing dialog.\n";
        capture.start({});
        auto started = std::chrono::steady_clock::now();
        auto sample_start = started;
        auto sample_frames = std::uint64_t{};
        double minimum_fps = 1000000.0;
        unsigned sample_count{};
        bool first = true;
        while (!interrupted) {
            const bool delivered = capture.poll([](const auto &frame) {
                if (!frame.native_image || frame.plane_count == 0 || frame.width != 1920 ||
                    frame.height != 1080)
                    throw std::runtime_error("Invalid GPU frame");
            });
            const auto now = std::chrono::steady_clock::now();
            if (first && delivered) {
                started = sample_start = now;
                sample_frames = capture.metrics().delivered;
                first = false;
            }
            const auto stats = capture.metrics();
            const double interval = std::chrono::duration<double>(now - sample_start).count();
            if (!first && interval >= 1.0) {
                const double fps = static_cast<double>(stats.delivered - sample_frames) / interval;
                minimum_fps = std::min(minimum_fps, fps);
                ++sample_count;
                std::cout << "{\"event\":\"capture.interval\",\"fps\":" << fps
                          << ",\"discarded\":" << stats.discarded
                          << ",\"sequenceGaps\":" << stats.sequence_gaps << "}\n";
                sample_frames = stats.delivered;
                sample_start = now;
            }
            if (!first && now - started >= std::chrono::seconds(seconds))
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        const double elapsed =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        capture.stop();
        const auto stats = capture.metrics();
        const double fps = elapsed > 0 ? static_cast<double>(stats.delivered) / elapsed : 0;
        // Strict capture gate, not encoder/WebRTC acceptance. Static sources may be damage-driven.
        const bool stable = !interrupted && seconds >= 30 && sample_count >= 29 &&
                            minimum_fps >= 58.0 && fps >= 59.0 && stats.discarded == 0 &&
                            stats.sequence_gaps == 0 && stats.delivered == stats.gpu_imports &&
                            stats.cpu_frames == 0 && stats.latency_samples > 0;
        std::cout << "{\"event\":\"capture.summary\",\"stable1080p60Gpu\":"
                  << (stable ? "true" : "false") << ",\"fps\":" << fps
                  << ",\"minimumIntervalFps\":" << (sample_count ? minimum_fps : 0)
                  << ",\"received\":" << stats.received << ",\"delivered\":" << stats.delivered
                  << ",\"discarded\":" << stats.discarded
                  << ",\"sequenceGaps\":" << stats.sequence_gaps << ",\"width\":" << stats.width
                  << ",\"height\":" << stats.height << ",\"drmFourcc\":" << stats.drm_format
                  << ",\"modifier\":" << stats.modifier << ",\"gpuImports\":" << stats.gpu_imports
                  << ",\"cpuFrames\":" << stats.cpu_frames << ",\"cpuCopies\":" << stats.cpu_copies
                  << ",\"gpuCopies\":" << stats.gpu_copies
                  << ",\"latencySamples\":" << stats.latency_samples << ",\"meanLatencyMs\":";
        if (stats.latency_samples)
            std::cout << stats.latency_sum_ms / static_cast<double>(stats.latency_samples);
        else
            std::cout << "null";
        std::cout << ",\"maxLatencyMs\":" << stats.max_latency_ms
                  << ",\"internalDriverCopies\":null,\"nvencInteropVerified\":false}\n";
        return stable ? 0 : 3;
    } catch (const cloudplay::capture::FrameCaptureError &error) {
        capture.stop();
        std::cerr << "{\"event\":\"capture.failed\",\"reason\":" << static_cast<int>(error.reason)
                  << ",\"operation\":\"" << error.operation
                  << "\",\"nativeCode\":" << error.native_code << ",\"stable1080p60Gpu\":false}\n";
        return 1;
    } catch (...) {
        capture.stop();
        std::cerr << "{\"event\":\"capture.failed\",\"reason\":\"unexpected\"}\n";
        return 1;
    }
}

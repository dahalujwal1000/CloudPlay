#include "cpu_snapshot.hpp"
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cloudplay/capture/capture_acceptance.hpp>
#include <cloudplay/capture/pipewire_capture.hpp>
#include <csignal>
#include <exception>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>

namespace {
volatile std::sig_atomic_t interrupted{};
void interrupt(int) { interrupted = 1; }
} // namespace

int main(int argc, char **argv) {
    std::cout << std::unitbuf;
    unsigned seconds{30};
    std::string snapshot_path, reference_path;
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        std::cout << "Usage: cloudplay_linux_capture_probe [--seconds 1..120] "
                     "[--snapshot new.png [--reference expected.png]]\n";
        return 0;
    }
    for (int i = 1; i < argc; i += 2) {
        if (i + 1 >= argc)
            return 2;
        const std::string_view option(argv[i]);
        const std::string_view value(argv[i + 1]);
        if (option == "--snapshot" && snapshot_path.empty())
            snapshot_path = value;
        else if (option == "--reference" && reference_path.empty())
            reference_path = value;
        else if (option == "--seconds") {
            const auto [end, error] =
                std::from_chars(value.data(), value.data() + value.size(), seconds);
            if (error != std::errc{} || end != value.data() + value.size() || seconds < 1 ||
                seconds > 120)
                return 2;
        } else
            return 2;
    }
    if (!reference_path.empty() && snapshot_path.empty())
        return 2;
#ifndef CLOUDPLAY_HAVE_PNG
    if (!snapshot_path.empty()) {
        std::cerr << "{\"event\":\"capture.failed\",\"operation\":"
                     "\"snapshot.libpng_development_dependency_missing\"}\n";
        return 1;
    }
#endif
    std::signal(SIGINT, interrupt);
    std::signal(SIGTERM, interrupt);
    cloudplay::capture::PipeWireCapture capture;
    try {
        cloudplay::capture::CpuImage reference;
        if (!reference_path.empty())
            reference = cloudplay::capture::load_png(reference_path);
        bool layout_logged{}, snapshot_done{};
        std::exception_ptr snapshot_error;
        cloudplay::capture::CpuImage snapshot;
        std::cerr << "Select a 1920x1080 monitor or window in GNOME's sharing dialog.\n";
        capture.start({});
        auto started = std::chrono::steady_clock::now();
        auto sample_start = started;
        auto sample_frames = std::uint64_t{};
        double minimum_fps = 1000000.0;
        unsigned sample_count{};
        bool first = true;
        while (!interrupted) {
            const bool delivered = capture.poll([&](const auto &frame) {
                if (!frame.native_image || frame.plane_count == 0 || frame.width != 1920 ||
                    frame.height != 1080)
                    throw std::runtime_error("Invalid GPU frame");
                if (!layout_logged) {
                    std::cout << "{\"event\":\"capture.format\",\"width\":" << frame.width
                              << ",\"height\":" << frame.height
                              << ",\"drmFourcc\":" << frame.drm_format
                              << ",\"modifier\":" << frame.modifier << "}\n";
                    cloudplay::capture::log_buffer_diagnostics(frame.diagnostics);
                    layout_logged = true;
                }
                if (!snapshot_path.empty() && !snapshot_done && !snapshot_error) {
                    try {
                        snapshot = cloudplay::capture::read_cpu_snapshot(frame);
                        snapshot_done = true;
                    } catch (...) {
                        snapshot_error = std::current_exception();
                    }
                }
            });
            if (snapshot_error)
                std::rethrow_exception(snapshot_error);
            const auto now = std::chrono::steady_clock::now();
            if (first && delivered) {
                started = sample_start = now;
                sample_frames = capture.metrics().delivered;
                first = false;
            }
            const auto stats = capture.metrics();
            if (snapshot_done)
                break;
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
        if (snapshot_done) {
            cloudplay::capture::save_png(snapshot, snapshot_path);
            std::uint64_t mismatched_pixels{};
            unsigned max_difference{};
            const bool same_size =
                reference.width == snapshot.width && reference.height == snapshot.height;
            if (!reference_path.empty() && same_size) {
                for (std::size_t i = 0; i < snapshot.rgb.size(); i += 3) {
                    bool mismatch{};
                    for (unsigned c = 0; c < 3; ++c) {
                        const auto difference = static_cast<unsigned>(
                            std::abs(static_cast<int>(snapshot.rgb[i + c]) - reference.rgb[i + c]));
                        max_difference = std::max(max_difference, difference);
                        mismatch = mismatch || difference != 0;
                    }
                    mismatched_pixels += mismatch ? 1 : 0;
                }
            }
            const bool exact = !reference_path.empty() && same_size && mismatched_pixels == 0;
            std::cout << "{\"event\":\"capture.snapshot\",\"saved\":true,\"cpuCopies\":1,"
                         "\"cpuAccess\":\"DMA_BUF_IOCTL_SYNC START/END READ\","
                         "\"conversion\":\"packed bytes to RGB; no scaling/rotation\","
                         "\"referenceProvided\":"
                      << (!reference_path.empty() ? "true" : "false")
                      << ",\"sameResolution\":" << (same_size ? "true" : "false")
                      << ",\"mismatchedPixels\":" << mismatched_pixels
                      << ",\"maximumChannelDifference\":" << max_difference
                      << ",\"pixelCorrectVerified\":" << (exact ? "true" : "false")
                      << ",\"stable1080p60Gpu\":false}\n";
            return reference_path.empty() || exact ? 0 : 3;
        }
        const double fps = elapsed > 0 ? static_cast<double>(stats.delivered) / elapsed : 0;
        // Strict capture gate, not encoder/WebRTC acceptance. Static sources may be damage-driven.
        const bool stable = cloudplay::capture::stable_gpu_capture(stats, elapsed, minimum_fps,
                                                                   sample_count, interrupted != 0);
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
                  << ",\"latencySamples\":" << stats.latency_samples
                  << ",\"meanNonnegativePresentationAgeMs\":";
        if (stats.latency_samples)
            std::cout << stats.latency_sum_ms / static_cast<double>(stats.latency_samples);
        else
            std::cout << "null";
        std::cout << ",\"maxNonnegativePresentationAgeMs\":" << stats.max_latency_ms
                  << ",\"presentationAgeSamples\":" << stats.presentation_age_samples
                  << ",\"futureTimestamps\":" << stats.future_timestamps
                  << ",\"minimumPresentationAgeMs\":" << stats.minimum_presentation_age_ms
                  << ",\"meanPresentationAgeMs\":";
        if (stats.presentation_age_samples)
            std::cout << stats.presentation_age_sum_ms /
                             static_cast<double>(stats.presentation_age_samples);
        else
            std::cout << "null";
        std::cout << ",\"presentationIntervals\":" << stats.presentation_intervals
                  << ",\"meanPresentationIntervalMs\":";
        if (stats.presentation_intervals)
            std::cout << stats.presentation_interval_sum_ms /
                             static_cast<double>(stats.presentation_intervals);
        else
            std::cout << "null";
        std::cout << ",\"maxPresentationIntervalMs\":" << stats.max_presentation_interval_ms
                  << ",\"negotiatedFps\":" << stats.negotiated_fps
                  << ",\"negotiatedMaxFps\":" << stats.negotiated_max_fps
                  << ",\"meanGpuImportMs\":";
        if (stats.gpu_imports)
            std::cout << stats.gpu_import_time_sum_ms / static_cast<double>(stats.gpu_imports);
        else
            std::cout << "null";
        std::cout << ",\"maxGpuImportMs\":" << stats.max_gpu_import_time_ms
                  << ",\"captureOriginLatencyVerified\":false"
                  << ",\"internalDriverCopies\":null,\"nvencInteropVerified\":false}\n";
        return stable ? 0 : 3;
    } catch (const cloudplay::capture::FrameCaptureError &error) {
        capture.stop();
        const auto stats = capture.metrics();
        cloudplay::capture::log_buffer_diagnostics(stats.buffer);
        std::cerr << "{\"event\":\"capture.failed\",\"reason\":" << static_cast<int>(error.reason)
                  << ",\"operation\":\"" << error.operation
                  << "\",\"nativeCode\":" << error.native_code << ",\"width\":" << stats.width
                  << ",\"height\":" << stats.height << ",\"drmFourcc\":" << stats.drm_format
                  << ",\"gpuImports\":" << stats.gpu_imports
                  << ",\"cpuFrames\":" << stats.cpu_frames << ",\"discarded\":" << stats.discarded
                  << ",\"negotiatedFps\":" << stats.negotiated_fps
                  << ",\"negotiatedMaxFps\":" << stats.negotiated_max_fps
                  << ",\"maxGpuImportMs\":" << stats.max_gpu_import_time_ms
                  << ",\"stable1080p60Gpu\":false}\n";
        return 1;
    } catch (...) {
        capture.stop();
        std::cerr << "{\"event\":\"capture.failed\",\"reason\":\"unexpected\"}\n";
        return 1;
    }
}

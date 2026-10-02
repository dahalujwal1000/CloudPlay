#include "cpu_snapshot.hpp"
#include <charconv>
#include <chrono>
#include <cloudplay/capture/pipewire_capture.hpp>
#include <csignal>
#include <exception>
#include <iostream>
#include <string_view>
#include <thread>
#include <unistd.h>

namespace cloudplay::capture {
namespace {
volatile std::sig_atomic_t cancelled{};
void cancel(int) { cancelled = 1; }
std::uint64_t checksum(const CpuImage &image) {
    std::uint64_t value = 14695981039346656037ULL;
    for (const auto byte : image.rgb)
        value = (value ^ byte) * 1099511628211ULL;
    return value;
}
} // namespace

int run_capture_diagnostics(int argc, char **argv) {
    unsigned frames{8}, seconds{30};
    bool cpu{};
    std::string reference_path, output_path, generate_path;
    for (int i = 1; i < argc; ++i) {
        const std::string_view option(argv[i]);
        if (option == "--capture-diagnostics")
            continue;
        if (option == "--cpu-capture") {
            cpu = true;
            continue;
        }
        if (++i >= argc || std::string_view(argv[i]).empty())
            return 2;
        const std::string_view value(argv[i]);
        if (option == "--snapshot")
            output_path = value;
        else if (option == "--reference")
            reference_path = value;
        else if (option == "--generate-reference")
            generate_path = value;
        else if (option == "--frames" || option == "--seconds") {
            auto &target = option == "--frames" ? frames : seconds;
            const auto [end, error] =
                std::from_chars(value.data(), value.data() + value.size(), target);
            if (error != std::errc{} || end != value.data() + value.size() || target < 1 ||
                target > 120)
                return 2;
        } else
            return 2;
    }
    PipeWireCapture capture;
    try {
        if (!generate_path.empty()) {
            save_png(make_reference_pattern(), generate_path);
            std::cout
                << "{\"event\":\"capture.reference_generated\",\"width\":1920,\"height\":1080}\n";
            return 0;
        }
        if (output_path.empty())
            output_path = "/tmp/cloudplay-diagnostic-" + std::to_string(getpid()) + ".png";
        CpuImage reference;
        if (!reference_path.empty())
            reference = load_png(reference_path);
        std::signal(SIGINT, cancel);
        std::signal(SIGTERM, cancel);
        capture.start({1920, 1080, 60, true, cpu});
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
        unsigned checked{}, mismatches{};
        while (checked < frames && !cancelled && std::chrono::steady_clock::now() < deadline) {
            CpuImage image;
            std::uint64_t owned_checksum{};
            std::exception_ptr failure;
            const bool delivered = capture.poll([&](const CapturedFrame &frame) {
                log_frame_diagnostics(frame);
                try {
                    image = read_cpu_snapshot(frame);
                    owned_checksum = checksum(image);
                    std::cout
                        << "{\"event\":\"capture.cpu_copy_complete\",\"bufferStillLeased\":true,"
                           "\"cpuMapped\":true,\"bytes\":"
                        << image.rgb.size() << "}\n";
                } catch (...) {
                    failure = std::current_exception();
                }
            });
            if (failure)
                std::rethrow_exception(failure);
            if (!delivered) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            const auto stats = capture.metrics();
            if (stats.received != stats.released || checksum(image) != owned_checksum)
                throw FrameCaptureError(FrameCaptureFailure::Consumer,
                                        "diagnostic.lifecycle_or_copy_changed");
            ++checked;
            const auto path =
                checked == 1 ? output_path : output_path + "." + std::to_string(checked) + ".png";
            save_png(image, path);
            std::cerr << "Saved CPU copy: " << path << '\n';
            const auto comparison = compare_pixels(image, reference);
            const bool exact = !reference_path.empty() && comparison.same_size &&
                               comparison.mismatched_pixels == 0;
            if (!reference_path.empty() && !exact)
                ++mismatches;
            std::cout << "{\"event\":\"capture.pixel_comparison\",\"frame\":" << checked
                      << ",\"bufferReleasedBeforePng\":true,\"ownedCopyUnchanged\":true,"
                         "\"pixelCorrect\":"
                      << (exact ? "true" : "false")
                      << ",\"sameResolution\":" << (comparison.same_size ? "true" : "false")
                      << ",\"mismatchedPixels\":";
            if (comparison.same_size)
                std::cout << comparison.mismatched_pixels;
            else
                std::cout << "null";
            std::cout << ",\"maximumChannelDifference\":";
            if (comparison.same_size)
                std::cout << comparison.max_difference;
            else
                std::cout << "null";
            std::cout << "}\n";
        }
        capture.stop();
        const auto stats = capture.metrics();
        const bool complete = checked == frames && !cancelled;
        const bool exact = complete && !reference_path.empty() && mismatches == 0;
        std::cout << "{\"event\":\"capture.diagnostic_summary\",\"framesChecked\":" << checked
                  << ",\"mismatchedFrames\":" << mismatches << ",\"received\":" << stats.received
                  << ",\"released\":" << stats.released
                  << ",\"pixelCorrectVerified\":" << (exact ? "true" : "false")
                  << ",\"stable1080p60Gpu\":false,"
                     "\"nvencInteropVerified\":false}\n";
        return complete && (reference_path.empty() || exact) ? 0 : 3;
    } catch (const FrameCaptureError &error) {
        capture.stop();
        const auto stats = capture.metrics();
        log_buffer_diagnostics(stats.buffer);
        std::cerr << "{\"event\":\"capture.diagnostic_failed\",\"operation\":\"" << error.operation
                  << "\",\"nativeCode\":" << error.native_code << ",\"received\":" << stats.received
                  << ",\"released\":" << stats.released << "}\n";
        return 1;
    } catch (...) {
        capture.stop();
        return 1;
    }
}
} // namespace cloudplay::capture

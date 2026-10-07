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
void log_json_string(std::ostream &out, std::string_view value) {
    constexpr char hex[] = "0123456789abcdef";
    out << '"';
    for (const unsigned char c : value) {
        if (c == '"' || c == '\\')
            out << '\\' << static_cast<char>(c);
        else if (c < 32 || c >= 127)
            out << "\\u00" << hex[c >> 4] << hex[c & 15];
        else
            out << static_cast<char>(c);
    }
    out << '"';
}
void log_timings(const cloudplay::capture::PipeWireCapture &capture) {
    const auto summaries = capture.timing_summary();
    std::cout
        << "{\"event\":\"capture.timings\",\"unit\":\"ms\",\"percentileMethod\":\"nearest-rank "
           "bounded prefix\",\"histogramUpperBoundsMs\":[8,15,18,25,35,50,null],\"stages\":{";
    for (std::size_t i = 0; i < summaries.size(); ++i) {
        const auto &s = summaries[i];
        if (i)
            std::cout << ',';
        std::cout << '"' << cloudplay::capture::capture_timing_names[i]
                  << "\":{\"count\":" << s.count << ",\"overflow\":" << s.overflow
                  << ",\"mean\":" << s.mean_ms << ",\"min\":" << s.min_ms << ",\"max\":" << s.max_ms
                  << ",\"p50\":" << s.p50_ms << ",\"p95\":" << s.p95_ms << ",\"p99\":" << s.p99_ms
                  << ",\"histogram\":[";
        for (std::size_t bin = 0; bin < s.histogram.size(); ++bin) {
            if (bin)
                std::cout << ',';
            std::cout << s.histogram[bin];
        }
        std::cout << "]}";
    }
    std::cout << "}}\n";
}
} // namespace

int main(int argc, char **argv) {
    std::cout << std::unitbuf;
    for (int i = 1; i < argc; ++i)
        if (std::string_view(argv[i]) == "--capture-diagnostics")
            return cloudplay::capture::run_capture_diagnostics(argc, argv);
    unsigned seconds{30};
    std::string snapshot_path, reference_path;
    bool desktop_validation{};
    bool fixed_rate{}, timing_diagnostics{};
    bool cpu_capture{}, pool_given{};
    unsigned buffer_pool_size{4};
    unsigned diagnostic_max_fps{60};
    bool maximum_given{};
    cloudplay::capture::CaptureSource source{cloudplay::capture::CaptureSource::Any};
    bool source_given{};
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        std::cout << "Usage: cloudplay_linux_capture_probe [--seconds 1..120] "
                     "[--fixed-rate] [--timing-diagnostics] [--cpu-capture] [--buffer-pool 2..8] "
                     "[--source any|monitor|window] "
                     "[--diagnostic-max-fps 60..65] "
                     "[--snapshot new.png [--reference expected.png]]\n"
                     "  --desktop-validation new.png [--seconds 30..120]\n"
                     "  --capture-diagnostics [--cpu-capture] [--frames 1..120] "
                     "[--seconds 1..120] [--snapshot new.png] [--reference expected.png]\n"
                     "  --capture-diagnostics --generate-reference new.png\n";
        return 0;
    }
    for (int i = 1; i < argc; ++i) {
        const std::string_view option(argv[i]);
        if (option == "--cpu-capture" && !cpu_capture) {
            cpu_capture = true;
            continue;
        }
        if (option == "--fixed-rate" && !fixed_rate) {
            fixed_rate = true;
            continue;
        }
        if (option == "--timing-diagnostics" && !timing_diagnostics) {
            timing_diagnostics = true;
            continue;
        }
        if (i + 1 >= argc)
            return 2;
        const std::string_view value(argv[++i]);
        if (option == "--diagnostic-max-fps" && !maximum_given) {
            maximum_given = true;
            const auto [end, error] =
                std::from_chars(value.data(), value.data() + value.size(), diagnostic_max_fps);
            if (error != std::errc{} || end != value.data() + value.size() ||
                diagnostic_max_fps < 60 || diagnostic_max_fps > 65)
                return 2;
        } else if (option == "--buffer-pool" && !pool_given) {
            pool_given = true;
            const auto [end, error] =
                std::from_chars(value.data(), value.data() + value.size(), buffer_pool_size);
            if (error != std::errc{} || end != value.data() + value.size() ||
                buffer_pool_size < 2 || buffer_pool_size > 8)
                return 2;
        } else if (option == "--source" && !source_given) {
            source_given = true;
            if (value == "monitor")
                source = cloudplay::capture::CaptureSource::Monitor;
            else if (value == "window")
                source = cloudplay::capture::CaptureSource::Window;
            else if (value != "any")
                return 2;
        } else if (option == "--snapshot" && snapshot_path.empty())
            snapshot_path = value;
        else if (option == "--desktop-validation" && snapshot_path.empty()) {
            snapshot_path = value;
            desktop_validation = true;
        } else if (option == "--reference" && reference_path.empty())
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
    if (fixed_rate && diagnostic_max_fps != 60)
        return 2;
    if (cpu_capture && !snapshot_path.empty())
        return 2;
    if (desktop_validation && (seconds < 30 || !reference_path.empty()))
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
        cloudplay::capture::CpuImage final_snapshot;
        bool capture_final{};
        unsigned diagnostic_copies{};
        std::cerr << "Select a 1920x1080 monitor or window in GNOME's sharing dialog.\n";
        cloudplay::capture::CaptureOptions options;
        options.embedded_cursor = desktop_validation;
        options.fixed_rate = fixed_rate;
        options.diagnostic_max_fps = diagnostic_max_fps;
        options.timing_diagnostics = timing_diagnostics;
        options.source = source;
        options.cpu_capture = cpu_capture;
        options.buffer_pool_size = buffer_pool_size;
        std::cout << "{\"event\":\"capture.requested_rate\",\"fixedRate\":"
                  << (fixed_rate ? "true" : "false") << ",\"minimumFps\":" << (fixed_rate ? 60 : 0)
                  << ",\"maximumFps\":" << diagnostic_max_fps
                  << ",\"timingDiagnostics\":" << (timing_diagnostics ? "true" : "false")
                  << ",\"cpuCapture\":" << (cpu_capture ? "true" : "false")
                  << ",\"requestedBufferPoolSize\":" << buffer_pool_size
                  << ",\"requestedSourceTypes\":" << static_cast<std::uint32_t>(source) << "}\n";
        capture.start(options);
        std::cout << "{\"event\":\"capture.source\",\"requestedSourceTypes\":"
                  << static_cast<std::uint32_t>(source)
                  << ",\"actualSourceType\":" << capture.metrics().source_type
                  << ",\"nodeId\":" << capture.metrics().source_node << "}\n";
        auto started = std::chrono::steady_clock::now();
        auto sample_start = started;
        auto sample_frames = std::uint64_t{};
        double minimum_fps = 1000000.0;
        unsigned sample_count{};
        bool first = true;
        std::cout << "{\"event\":\"capture.validation_mode\",\"embeddedCursorRequested\":"
                  << (desktop_validation ? "true" : "false") << "}\n";
        while (!interrupted) {
            const bool delivered = capture.poll([&](const auto &frame) {
                const bool valid_storage =
                    cpu_capture ? frame.storage == cloudplay::capture::FrameStorage::CpuMemory &&
                                      frame.cpu_data && frame.plane_count == 1
                                : frame.storage == cloudplay::capture::FrameStorage::DmaBuf &&
                                      frame.native_image && frame.plane_count > 0;
                if (!valid_storage || frame.width != 1920 || frame.height != 1080)
                    throw std::runtime_error("Invalid capture frame");
                if (!layout_logged) {
                    std::cout << "{\"event\":\"capture.format\",\"width\":" << frame.width
                              << ",\"height\":" << frame.height
                              << ",\"drmFourcc\":" << frame.drm_format
                              << ",\"modifier\":" << frame.modifier << "}\n";
                    cloudplay::capture::log_buffer_diagnostics(frame.diagnostics);
                    layout_logged = true;
                }
                const bool snapshot_due =
                    !desktop_validation || (!first && std::chrono::steady_clock::now() - started >=
                                                          std::chrono::seconds(5));
                if (!snapshot_path.empty() && !snapshot_error &&
                    ((!snapshot_done && snapshot_due) || capture_final)) {
                    try {
                        if (capture_final) {
                            final_snapshot = cloudplay::capture::read_cpu_snapshot(frame);
                            capture_final = false;
                        } else {
                            snapshot = cloudplay::capture::read_cpu_snapshot(frame);
                            snapshot_done = true;
                        }
                        ++diagnostic_copies;
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
            if (snapshot_done && !desktop_validation)
                break;
            const double interval = std::chrono::duration<double>(now - sample_start).count();
            if (!first && interval >= 1.0) {
                const double fps = static_cast<double>(stats.delivered - sample_frames) / interval;
                minimum_fps = std::min(minimum_fps, fps);
                ++sample_count;
                std::cout << "{\"event\":\"capture.interval\",\"fps\":" << fps
                          << ",\"discarded\":" << stats.discarded
                          << ",\"bufferPoolSize\":" << stats.buffer_pool_size
                          << ",\"outstandingBuffers\":" << (stats.received - stats.released)
                          << ",\"maxOutstandingBuffers\":" << stats.max_outstanding_buffers
                          << ",\"dequeueBatches\":" << stats.dequeue_batches
                          << ",\"maxDequeueBatch\":" << stats.max_dequeue_batch
                          << ",\"sequenceGaps\":" << stats.sequence_gaps << "}\n";
                sample_frames = stats.delivered;
                sample_start = now;
            }
            if (!first && now - started >= std::chrono::seconds(seconds)) {
                if (!desktop_validation || !final_snapshot.rgb.empty())
                    break;
                capture_final = true;
            }
            if (!first && now - started >= std::chrono::seconds(seconds + 10))
                throw cloudplay::capture::FrameCaptureError(
                    cloudplay::capture::FrameCaptureFailure::Timeout,
                    "diagnostic.final_frame_timeout");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        const double elapsed =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        capture.stop();
        if (timing_diagnostics)
            log_timings(capture);
        const auto stats = capture.metrics();
        if (snapshot_done && desktop_validation) {
            cloudplay::capture::save_png(snapshot, snapshot_path);
            if (!final_snapshot.rgb.empty())
                cloudplay::capture::save_png(final_snapshot, snapshot_path + ".last.png");
            std::cout << "{\"event\":\"capture.desktop_snapshots\",\"cpuCopies\":"
                      << diagnostic_copies
                      << ",\"finalSaved\":" << (!final_snapshot.rgb.empty() ? "true" : "false")
                      << ",\"pixelCorrectVerified\":false,\"motionCorrectVerified\":false}\n";
        }
        if (snapshot_done && !desktop_validation) {
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
        const bool stable = !desktop_validation && !cpu_capture && diagnostic_max_fps == 60 &&
                            cloudplay::capture::stable_gpu_capture(stats, elapsed, minimum_fps,
                                                                   sample_count, interrupted != 0);
        std::cout << "{\"event\":\"capture.summary\",\"stable1080p60Gpu\":"
                  << (stable ? "true" : "false") << ",\"fps\":" << fps
                  << ",\"diagnosticMaxFps\":" << diagnostic_max_fps
                  << ",\"cpuCapture\":" << (cpu_capture ? "true" : "false")
                  << ",\"elapsedSeconds\":" << elapsed
                  << ",\"fixedRateRequested\":" << (fixed_rate ? "true" : "false")
                  << ",\"actualSourceType\":" << stats.source_type
                  << ",\"nodeId\":" << stats.source_node
                  << ",\"diagnosticCpuCopies\":" << diagnostic_copies
                  << ",\"minimumIntervalFps\":" << (sample_count ? minimum_fps : 0)
                  << ",\"received\":" << stats.received << ",\"delivered\":" << stats.delivered
                  << ",\"released\":" << stats.released << ",\"discarded\":" << stats.discarded
                  << ",\"sequenceGaps\":" << stats.sequence_gaps << ",\"width\":" << stats.width
                  << ",\"height\":" << stats.height << ",\"drmFourcc\":" << stats.drm_format
                  << ",\"modifier\":" << stats.modifier << ",\"gpuImports\":" << stats.gpu_imports
                  << ",\"requestedBufferPoolSize\":" << stats.requested_buffer_pool_size
                  << ",\"bufferPoolSize\":" << stats.buffer_pool_size
                  << ",\"maxBufferPoolSize\":" << stats.max_buffer_pool_size
                  << ",\"requestedMinBufferPoolSize\":" << stats.requested_min_buffer_pool_size
                  << ",\"requestedMaxBufferPoolSize\":" << stats.requested_max_buffer_pool_size
                  << ",\"dequeueBatches\":" << stats.dequeue_batches
                  << ",\"multiDequeueBatches\":" << stats.multi_dequeue_batches
                  << ",\"maxDequeueBatch\":" << stats.max_dequeue_batch
                  << ",\"outstandingBuffers\":" << (stats.received - stats.released)
                  << ",\"maxOutstandingBuffers\":" << stats.max_outstanding_buffers
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
        if (timing_diagnostics)
            log_timings(capture);
        const auto stats = capture.metrics();
        cloudplay::capture::log_buffer_diagnostics(stats.buffer);
        std::cerr << "{\"event\":\"capture.failed\",\"reason\":" << static_cast<int>(error.reason)
                  << ",\"operation\":\"" << error.operation
                  << "\",\"nativeCode\":" << error.native_code << ",\"width\":" << stats.width
                  << ",\"height\":" << stats.height << ",\"drmFourcc\":" << stats.drm_format
                  << ",\"gpuImports\":" << stats.gpu_imports << ",\"received\":" << stats.received
                  << ",\"released\":" << stats.released << ",\"delivered\":" << stats.delivered
                  << ",\"sequenceGaps\":" << stats.sequence_gaps
                  << ",\"requestedBufferPoolSize\":" << stats.requested_buffer_pool_size
                  << ",\"bufferPoolSize\":" << stats.buffer_pool_size
                  << ",\"maxBufferPoolSize\":" << stats.max_buffer_pool_size
                  << ",\"requestedMinBufferPoolSize\":" << stats.requested_min_buffer_pool_size
                  << ",\"requestedMaxBufferPoolSize\":" << stats.requested_max_buffer_pool_size
                  << ",\"dequeueBatches\":" << stats.dequeue_batches
                  << ",\"multiDequeueBatches\":" << stats.multi_dequeue_batches
                  << ",\"maxDequeueBatch\":" << stats.max_dequeue_batch
                  << ",\"outstandingBuffers\":" << (stats.received - stats.released)
                  << ",\"maxOutstandingBuffers\":" << stats.max_outstanding_buffers
                  << ",\"cpuFrames\":" << stats.cpu_frames << ",\"discarded\":" << stats.discarded
                  << ",\"negotiatedFps\":" << stats.negotiated_fps
                  << ",\"negotiatedMaxFps\":" << stats.negotiated_max_fps
                  << ",\"maxGpuImportMs\":" << stats.max_gpu_import_time_ms
                  << ",\"fixedRateRequested\":" << (fixed_rate ? "true" : "false")
                  << ",\"requestedSourceTypes\":" << static_cast<std::uint32_t>(source)
                  << ",\"actualSourceType\":" << stats.source_type
                  << ",\"nodeId\":" << stats.source_node
                  << ",\"pipewireErrorObject\":" << stats.error_object
                  << ",\"pipewireErrorSequence\":" << stats.error_sequence
                  << ",\"streamState\":" << stats.stream_state;
        if (timing_diagnostics) {
            std::cerr << ",\"backendError\":";
            log_json_string(std::cerr, stats.backend_error.data());
            std::cerr << ",\"backendErrorTruncated\":"
                      << (stats.backend_error_truncated ? "true" : "false");
        }
        std::cerr << ",\"stable1080p60Gpu\":false}\n";
        return 1;
    } catch (...) {
        capture.stop();
        std::cerr << "{\"event\":\"capture.failed\",\"reason\":\"unexpected\"}\n";
        return 1;
    }
}

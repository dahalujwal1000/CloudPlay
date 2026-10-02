#include <cloudplay/capture/capture_acceptance.hpp>
#include <cloudplay/capture/frame_capture.hpp>
#include <cloudplay/capture/presentation_timing.hpp>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace {
void release(void *owner, void *resource) noexcept {
    if (resource)
        ++*static_cast<int *>(owner);
}
void release_order(void *owner, void *resource) noexcept {
    *static_cast<int *>(resource) = ++*static_cast<int *>(owner);
}
} // namespace

int main() {
    using cloudplay::capture::FrameLease;
    static_assert(!std::is_copy_constructible_v<FrameLease>);
    static_assert(!std::is_move_constructible_v<FrameLease>);
    int released{};
    int buffer{};
    {
        const FrameLease lease(&released, &buffer, release);
        if (released != 0)
            throw std::runtime_error("Frame returned before consumer completed");
    }
    if (released != 1)
        throw std::runtime_error("Frame not returned exactly once");
    try {
        const FrameLease lease(&released, &buffer, release);
        throw std::runtime_error("Consumer failed");
    } catch (const std::runtime_error &) {
    }
    if (released != 2)
        throw std::runtime_error("Exception leaked a frame");
    int order{};
    int image_release{}, buffer_release{};
    {
        const FrameLease frame(&order, &buffer_release, release_order);
        const FrameLease image(&order, &image_release, release_order);
    }
    if (image_release != 1 || buffer_release != 2)
        throw std::runtime_error("GPU image not released before buffer reuse");

    using namespace cloudplay::capture;
    FrameCaptureMetrics timing_metrics;
    PresentationTiming timing;
    timing.observe(timing_metrics, 1000000000, 1001000000);
    timing.observe(timing_metrics, 1016000000, 1015000000);
    if (timing_metrics.presentation_age_samples != 2 || timing_metrics.future_timestamps != 1 ||
        timing_metrics.latency_samples != 1 || timing_metrics.latency_sum_ms != 1.0 ||
        timing_metrics.minimum_presentation_age_ms != -1.0 ||
        timing_metrics.presentation_intervals != 1 ||
        timing_metrics.presentation_interval_sum_ms != 16.0)
        throw std::runtime_error("Incorrect presentation age or cadence");
    timing.observe(timing_metrics, 1032000000, 1032000000, true);
    timing.observe(timing_metrics, 0, 1048000000);
    timing.observe(timing_metrics, std::numeric_limits<std::int64_t>::min(), 1048000000);
    timing.observe(timing_metrics, std::numeric_limits<std::int64_t>::max(), 1048000000);
    timing.observe(timing_metrics, 1048000000, 1048000000);
    timing.observe(timing_metrics, 1048000000, 1048000000);
    timing.observe(timing_metrics, 1047000000, 1048000000);
    if (timing_metrics.presentation_intervals != 1 ||
        timing_metrics.presentation_age_samples != 6 ||
        timing_metrics.max_presentation_interval_ms != 16.0)
        throw std::runtime_error("Invalid or discontinuous timestamp counted as cadence");

    FrameCaptureMetrics good;
    good.delivered = good.gpu_imports = good.received = 1800;
    good.width = 1920;
    good.height = 1080;
    good.drm_format = 1;
    good.presentation_age_samples = 1800;
    if (!stable_gpu_capture(good, 30.0, 59.0, 29, false) ||
        stable_gpu_capture(good, 5.0, 60.0, 4, false) ||
        stable_gpu_capture(good, 30.0, 35.0, 29, false) ||
        stable_gpu_capture(good, 30.0, 59.0, 29, true) ||
        stable_gpu_capture(good, 60.0, 59.0, 29, false))
        throw std::runtime_error("Bad sustained capture gate");
    for (int condition = 0; condition < 6; ++condition) {
        auto bad = good;
        if (condition == 0)
            bad.gpu_imports = 0;
        if (condition == 1)
            bad.cpu_frames = 1;
        if (condition == 2)
            bad.discarded = 1;
        if (condition == 3)
            bad.sequence_gaps = 1;
        if (condition == 4)
            bad.width = 1280;
        if (condition == 5)
            bad.presentation_age_samples = 0;
        if (stable_gpu_capture(bad, 30.0, 59.0, 29, false))
            throw std::runtime_error("Invalid stream passed capture gate");
    }
}

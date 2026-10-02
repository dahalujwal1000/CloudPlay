#pragma once

#include <algorithm>
#include <cloudplay/capture/frame_capture.hpp>

namespace cloudplay::capture {

// Presentation timestamps describe producer cadence, not capture-origin latency.
class PresentationTiming final {
  public:
    void observe(FrameCaptureMetrics &metrics, std::int64_t pts, std::int64_t now,
                 bool discontinuity = false) noexcept {
        const auto age_ns = static_cast<long double>(now) - static_cast<long double>(pts);
        if (pts <= 0 || now <= 0 || age_ns <= -10000000000.0L || age_ns >= 10000000000.0L) {
            previous_ = 0;
            return;
        }
        const double age_ms = static_cast<double>(age_ns / 1000000.0L);
        ++metrics.presentation_age_samples;
        metrics.presentation_age_sum_ms += age_ms;
        metrics.minimum_presentation_age_ms = metrics.presentation_age_samples == 1
                                                 ? age_ms
                                                 : std::min(metrics.minimum_presentation_age_ms,
                                                            age_ms);
        if (age_ms < 0)
            ++metrics.future_timestamps;
        else {
            ++metrics.latency_samples;
            metrics.latency_sum_ms += age_ms;
            metrics.max_latency_ms = std::max(metrics.max_latency_ms, age_ms);
        }
        if (!discontinuity && previous_ > 0 && pts > previous_ &&
            pts - previous_ < 10000000000LL) {
            const double interval_ms = static_cast<double>(pts - previous_) / 1000000.0;
            ++metrics.presentation_intervals;
            metrics.presentation_interval_sum_ms += interval_ms;
            metrics.max_presentation_interval_ms =
                std::max(metrics.max_presentation_interval_ms, interval_ms);
        }
        previous_ = pts;
    }

  private:
    std::int64_t previous_{};
};

} // namespace cloudplay::capture

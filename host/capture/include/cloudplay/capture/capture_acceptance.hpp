#pragma once

#include <cloudplay/capture/frame_capture.hpp>
#include <cmath>

namespace cloudplay::capture {
[[nodiscard]] inline bool stable_gpu_capture(const FrameCaptureMetrics &stats, double elapsed,
                                             double minimum_interval_fps, unsigned intervals,
                                             bool interrupted) noexcept {
    if (interrupted || !std::isfinite(elapsed) || !std::isfinite(minimum_interval_fps) ||
        elapsed < 30.0 || intervals < 29)
        return false;
    const double fps = static_cast<double>(stats.delivered) / elapsed;
    return stats.width == 1920 && stats.height == 1080 && stats.drm_format != 0 &&
           minimum_interval_fps >= 58.0 && fps >= 59.0 && stats.discarded == 0 &&
           stats.sequence_gaps == 0 && stats.delivered == stats.gpu_imports &&
           stats.received == stats.delivered && stats.received == stats.released &&
           stats.cpu_frames == 0 && stats.cpu_copies == 0 && stats.gpu_copies == 0 &&
           stats.presentation_age_samples > 0;
}
} // namespace cloudplay::capture

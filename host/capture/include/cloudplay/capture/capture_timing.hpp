#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <vector>

namespace cloudplay::capture {

enum class CaptureTimingStage {
    Arrival,
    Producer,
    Fence,
    Import,
    Consumer,
    Cleanup,
    Requeue,
    Held,
    Poll,
    Count
};
inline constexpr std::array<const char *, 9> capture_timing_names{
    "arrivalInterval", "producerInterval", "fenceWait",  "eglImport",   "consumerCallback",
    "eglCleanup",      "bufferRequeue",    "bufferHeld", "pollInterval"};

struct TimingSummary {
    std::uint64_t count{}, overflow{};
    double mean_ms{}, min_ms{}, max_ms{}, p50_ms{}, p95_ms{}, p99_ms{};
    // Millisecond bins: [0,8), [8,15), [15,18), [18,25), [25,35), [35,50), [50,+inf).
    std::array<std::uint64_t, 7> histogram{};
};

// Owner-thread only. Aggregate statistics cover all samples; percentiles cover the bounded prefix.
class TimingSamples {
  public:
    TimingSamples() = default;
    explicit TimingSamples(std::size_t capacity) : capacity_(capacity) {}
    void prepare() { samples_.reserve(capacity_); }
    void observe(double ms) noexcept {
        if (!std::isfinite(ms) || ms < 0)
            return;
        if (summary_.count++ == 0)
            summary_.min_ms = ms;
        summary_.min_ms = std::min(summary_.min_ms, ms);
        summary_.max_ms = std::max(summary_.max_ms, ms);
        sum_ += ms;
        constexpr std::array limits{8.0, 15.0, 18.0, 25.0, 35.0, 50.0};
        const auto bin = std::upper_bound(limits.begin(), limits.end(), ms) - limits.begin();
        ++summary_.histogram[static_cast<std::size_t>(bin)];
        if (samples_.size() < samples_.capacity() && samples_.size() < capacity_)
            samples_.push_back(ms);
        else
            ++summary_.overflow;
    }
    [[nodiscard]] TimingSummary summary() const {
        auto result = summary_;
        if (result.count)
            result.mean_ms = sum_ / static_cast<double>(result.count);
        auto sorted = samples_;
        std::sort(sorted.begin(), sorted.end());
        if (!sorted.empty()) {
            const auto percentile = [&](double fraction) {
                return sorted[static_cast<std::size_t>(
                                  std::ceil(fraction * static_cast<double>(sorted.size()))) -
                              1];
            };
            result.p50_ms = percentile(0.50);
            result.p95_ms = percentile(0.95);
            result.p99_ms = percentile(0.99);
        }
        return result;
    }

  private:
    std::size_t capacity_{8192};
    std::vector<double> samples_;
    TimingSummary summary_;
    double sum_{};
};

class CaptureStageTimer {
  public:
    explicit CaptureStageTimer(TimingSamples *samples)
        : samples_(samples), started_(samples ? std::chrono::steady_clock::now()
                                              : std::chrono::steady_clock::time_point{}) {}
    ~CaptureStageTimer() {
        if (samples_)
            samples_->observe(std::chrono::duration<double, std::milli>(
                                  std::chrono::steady_clock::now() - started_)
                                  .count());
    }
    CaptureStageTimer(const CaptureStageTimer &) = delete;
    CaptureStageTimer &operator=(const CaptureStageTimer &) = delete;

  private:
    TimingSamples *samples_;
    std::chrono::steady_clock::time_point started_;
};

} // namespace cloudplay::capture

#pragma once

#include "cloudplay/capture/capture_timing.hpp"
#include <limits>
#include <optional>

namespace cloudplay::capture {

inline std::optional<std::uint64_t> presentation_ns(std::uint32_t hi, std::uint32_t lo,
                                                    std::uint32_t ns) noexcept {
    const auto seconds = (std::uint64_t{hi} << 32) | lo;
    if (ns >= 1'000'000'000 ||
        seconds > (std::numeric_limits<std::uint64_t>::max() - ns) / 1'000'000'000)
        return std::nullopt;
    return seconds * 1'000'000'000 + ns;
}

// Owner-thread only. Presentation time is not the frame-callback or capture PTS clock.
class SourcePresentationTiming {
  public:
    SourcePresentationTiming() { intervals.prepare(); }
    bool observe(std::uint32_t hi, std::uint32_t lo, std::uint32_t ns) noexcept {
        const auto value = presentation_ns(hi, lo, ns);
        if (!value || (count && *value <= last)) {
            ++invalid;
            return false;
        }
        if (count)
            intervals.observe(static_cast<double>(*value - last) / 1e6);
        else
            first = *value;
        last = *value;
        ++count;
        return true;
    }
    [[nodiscard]] double span_seconds() const noexcept {
        return count > 1 ? static_cast<double>(last - first) / 1e9 : 0;
    }
    [[nodiscard]] double fps() const noexcept {
        return count > 1 ? static_cast<double>(count - 1) / span_seconds() : 0;
    }
    TimingSamples intervals;
    std::uint64_t count{}, invalid{}, first{}, last{};
};
} // namespace cloudplay::capture

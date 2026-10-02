#pragma once

#include <cstdint>

namespace cloudplay::capture {

struct FrameSize {
    std::int32_t width{};
    std::int32_t height{};
    bool operator==(const FrameSize&) const = default;
};

enum class FrameDecision { Discard, RecreatePool, Deliver };

// Zero-sized/minimized content must never reach texture consumers.
constexpr FrameDecision decide_frame(FrameSize pool, FrameSize content) noexcept {
    if (content.width <= 0 || content.height <= 0) return FrameDecision::Discard;
    return pool == content ? FrameDecision::Deliver : FrameDecision::RecreatePool;
}

} // namespace cloudplay::capture

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace cloudplay::capture {

struct CapturePacingSample {
    std::int64_t arrival_ns{};
    std::optional<std::int64_t> producer_pts_ns;
    std::optional<std::uint64_t> sequence;
    bool discontinuity{}, corrupted{};
};

// Owner-thread only. Copy scalar metadata; never retain a buffer or write logs in process().
class CapturePacingTrace final {
  public:
    static constexpr std::size_t capacity = 8192;
    void prepare() { samples_.reserve(capacity); }
    void observe(CapturePacingSample sample) noexcept {
        ++observed_;
        if (samples_.size() < samples_.capacity() && samples_.size() < capacity)
            samples_.push_back(sample);
    }
    [[nodiscard]] std::span<const CapturePacingSample> samples() const noexcept { return samples_; }
    [[nodiscard]] std::uint64_t observed() const noexcept { return observed_; }
    [[nodiscard]] std::uint64_t overflow() const noexcept { return observed_ - samples_.size(); }

  private:
    std::vector<CapturePacingSample> samples_;
    std::uint64_t observed_{};
};

} // namespace cloudplay::capture

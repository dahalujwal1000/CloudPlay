#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace cloudplay {

struct StreamConfig {
    std::uint32_t width{1920};
    std::uint32_t height{1080};
    std::uint32_t fps{60};
    std::uint32_t bitrate_mbps{12};
};

[[nodiscard]] std::optional<std::string_view> validate(const StreamConfig &config) noexcept;

} // namespace cloudplay

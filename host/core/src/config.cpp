#include <cloudplay/core/config.hpp>

namespace cloudplay {

std::optional<std::string_view> validate(const StreamConfig &config) noexcept {
    if (config.width != 1920 || config.height != 1080 || config.fps != 60)
        return "Foundation supports only 1920x1080 at 60 FPS";
    if (config.bitrate_mbps < 6 || config.bitrate_mbps > 20)
        return "Bitrate must be between 6 and 20 Mbps";
    return std::nullopt;
}

} // namespace cloudplay

#include <cloudplay/games/game_profile.hpp>
#include <filesystem>
#include <string_view>

namespace cloudplay::games {
namespace {
bool bounded_text(std::string_view value, std::size_t maximum) {
    if (value.empty() || value.size() > maximum)
        return false;
    for (const auto ch : value)
        if (static_cast<unsigned char>(ch) < 32 || ch == 127)
            return false;
    return true;
}
bool absolute_path(const std::string &value) {
    return bounded_text(value, 4096) && std::filesystem::path(value).is_absolute();
}
} // namespace

std::optional<ProfileError> validate(const GameProfile &profile) {
    if (profile.id.empty() || profile.id.size() > 64)
        return ProfileError::Id;
    for (const auto ch : profile.id)
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
              ch == '-' || ch == '_'))
            return ProfileError::Id;
    if (!bounded_text(profile.display_name, 128))
        return ProfileError::DisplayName;
    if (!absolute_path(profile.executable))
        return ProfileError::Executable;
    if (!absolute_path(profile.working_directory))
        return ProfileError::WorkingDirectory;
    if (profile.arguments.size() > 64)
        return ProfileError::Arguments;
    std::size_t total{};
    for (const auto &argument : profile.arguments) {
        if (argument.size() > 4096 || argument.find('\0') != std::string::npos)
            return ProfileError::Arguments;
        total += argument.size() + 1;
        if (total > 32768)
            return ProfileError::Arguments;
    }
    return std::nullopt;
}
} // namespace cloudplay::games

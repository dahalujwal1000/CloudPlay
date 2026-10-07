#pragma once

#include <optional>
#include <string>
#include <vector>

namespace cloudplay::games {

// Trusted local configuration, never an arbitrary command supplied by a peer.
struct GameProfile {
    std::string id;
    std::string display_name;
    std::string executable;
    std::string working_directory;
    std::vector<std::string> arguments;
};
enum class ProfileError { Id, DisplayName, Executable, WorkingDirectory, Arguments };
[[nodiscard]] std::optional<ProfileError> validate(const GameProfile &profile);

} // namespace cloudplay::games

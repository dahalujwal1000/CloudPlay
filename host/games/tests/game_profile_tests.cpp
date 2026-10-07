#include <cloudplay/games/game_profile.hpp>
#include <filesystem>
#include <stdexcept>

namespace {
using namespace cloudplay::games;
void check(bool condition) {
    if (!condition)
        throw std::runtime_error("Game profile assertion failed");
}
GameProfile profile() {
    const auto directory = std::filesystem::current_path();
    return {"example-game",
            "Example Game",
            (directory / "game").string(),
            directory.string(),
            {"space arg", "", "$(literal);*"}};
}
} // namespace
int main() {
    check(!validate(profile()));
    for (const auto id : {"", "../game", "game name", "game\n"}) {
        auto invalid = profile();
        invalid.id = id;
        check(validate(invalid) == ProfileError::Id);
    }
    auto invalid = profile();
    invalid.id = std::string(65, 'a');
    check(validate(invalid) == ProfileError::Id);
    invalid = profile();
    invalid.display_name = "";
    check(validate(invalid) == ProfileError::DisplayName);
    invalid.display_name = std::string(129, 'a');
    check(validate(invalid) == ProfileError::DisplayName);
    invalid = profile();
    invalid.executable = "relative";
    check(validate(invalid) == ProfileError::Executable);
    invalid = profile();
    invalid.executable.push_back('\0');
    check(validate(invalid) == ProfileError::Executable);
    invalid = profile();
    invalid.working_directory = "relative";
    check(validate(invalid) == ProfileError::WorkingDirectory);
    invalid = profile();
    invalid.arguments.assign(65, "");
    check(validate(invalid) == ProfileError::Arguments);
    invalid.arguments.assign(64, "");
    check(!validate(invalid));
    invalid.arguments = {std::string(4097, 'a')};
    check(validate(invalid) == ProfileError::Arguments);
    invalid.arguments = {std::string("abc\0def", 7)};
    check(validate(invalid) == ProfileError::Arguments);
    invalid.arguments.assign(8, std::string(4095, 'a'));
    check(!validate(invalid));
    invalid.arguments[0].push_back('a');
    check(validate(invalid) == ProfileError::Arguments);
}

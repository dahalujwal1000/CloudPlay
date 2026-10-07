#include <chrono>
#include <cloudplay/games/linux_game_process.hpp>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <thread>
#include <type_traits>

namespace {
using namespace cloudplay::games;
void check(bool condition) {
    if (!condition)
        throw std::runtime_error("Linux game process assertion failed");
}
struct Directory {
    std::filesystem::path path;
    Directory() {
        auto pattern = (std::filesystem::temp_directory_path() / "cloudplay-games-XXXXXX").string();
        auto *created = mkdtemp(pattern.data());
        check(created);
        path = created;
    }
    ~Directory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};
void wait_exit(LinuxGameProcess &process) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (process.state() != ProcessState::Exited && std::chrono::steady_clock::now() < deadline) {
        process.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(process.state() == ProcessState::Exited && process.exit_result().has_value());
}
void wait_file(const std::filesystem::path &path) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!std::filesystem::exists(path) && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    check(std::filesystem::exists(path));
}
void launch_failure(GameProfile profile, ProcessFailure expected) {
    LinuxGameProcess process;
    try {
        process.launch(profile);
        throw std::runtime_error("Invalid game launch succeeded");
    } catch (const GameProcessError &error) {
        check(error.reason == expected);
    }
    check(process.state() == ProcessState::Failed && process.diagnostics().failures == 1);
    process.poll();
    check(!process.request_stop() && !process.detach());
}
} // namespace

int main(int argc, char **argv) {
    check(argc == 2);
    static_assert(!std::is_copy_constructible_v<LinuxGameProcess>);
    Directory directory;
    const GameProfile base{"fixture", "Fixture", argv[1], directory.path.string(), {"exit7"}};
    LinuxGameProcess idle;
    idle.poll();
    check(!idle.request_stop() && !idle.detach() && !idle.exit_result());
    {
        LinuxGameProcess process;
        auto profile = base;
        profile.arguments = {"verify", directory.path.string(), "space arg",
                             "$(touch should-not-exist);*", ""};
        const auto cwd = std::filesystem::current_path();
        process.launch(profile);
        check(std::filesystem::current_path() == cwd);
        wait_exit(process);
        check(process.exit_result()->exit_code == 0 && !process.exit_result()->signal);
        check(!std::filesystem::exists(directory.path / "should-not-exist"));
        try {
            process.launch(profile);
            return 1;
        } catch (const std::logic_error &) {
        }
    }
    {
        LinuxGameProcess process;
        process.launch(base);
        wait_exit(process);
        check(process.exit_result()->exit_code == 7 && !process.request_stop());
    }
    auto invalid = base;
    invalid.executable = "relative";
    launch_failure(invalid, ProcessFailure::InvalidProfile);
    invalid = base;
    invalid.executable = (directory.path / "missing").string();
    launch_failure(invalid, ProcessFailure::ExecutableUnavailable);
    invalid = base;
    invalid.working_directory = (directory.path / "missing").string();
    launch_failure(invalid, ProcessFailure::DirectoryUnavailable);
    const auto broken = directory.path / "missing-interpreter";
    {
        std::ofstream file(broken);
        file << "#!/cloudplay-test-interpreter-does-not-exist\n";
    }
    std::filesystem::permissions(broken, std::filesystem::perms::owner_read |
                                             std::filesystem::perms::owner_exec);
    invalid = base;
    invalid.executable = broken.string();
    launch_failure(invalid, ProcessFailure::Spawn);
    std::filesystem::permissions(broken, std::filesystem::perms::owner_read);
    launch_failure(invalid, ProcessFailure::ExecutableUnavailable);
    {
        LinuxGameProcess process;
        auto profile = base;
        profile.arguments = {"wait"};
        process.launch(profile);
        check(process.request_stop() && process.state() == ProcessState::StopRequested);
        wait_exit(process);
        check(process.exit_result()->signal == SIGTERM && process.diagnostics().stop_requests == 1);
    }
    {
        LinuxGameProcess process;
        auto profile = base;
        const auto ready = directory.path / "ready";
        profile.arguments = {"ignore-term", ready.string()};
        process.launch(profile);
        wait_file(ready);
        check(process.request_stop() && process.request_stop());
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        process.poll();
        check(process.state() == ProcessState::StopRequested &&
              process.diagnostics().stop_requests == 1);
        check(process.request_stop(StopMode::Force));
        wait_exit(process);
        check(process.exit_result()->signal == SIGKILL && process.diagnostics().stop_requests == 2);
    }
    for (const bool explicit_detach : {false, true}) {
        const auto marker = directory.path / (explicit_detach ? "detached" : "destroyed");
        {
            LinuxGameProcess process;
            auto profile = base;
            profile.arguments = {"touch-after", marker.string()};
            process.launch(profile);
            if (explicit_detach) {
                check(process.detach() && process.state() == ProcessState::Detached);
                check(!process.request_stop() && !process.detach());
            }
        }
        wait_file(marker);
    }
}

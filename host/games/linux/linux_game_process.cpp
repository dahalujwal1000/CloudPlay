#include <cloudplay/games/linux_game_process.hpp>
#include <csignal>
#include <filesystem>
#include <gio/gio.h>
#include <unistd.h>

namespace cloudplay::games {

struct LinuxGameProcess::Impl {
    GSubprocess *child{};
    ProcessState state{ProcessState::Idle};
    std::optional<ProcessExit> result;
    std::optional<StopMode> stop_mode;
    ProcessDiagnostics metrics;
    ~Impl() {
        if (child)
            g_object_unref(child);
    }
};
LinuxGameProcess::LinuxGameProcess() : impl_(std::make_unique<Impl>()) {}
LinuxGameProcess::~LinuxGameProcess() = default;
ProcessState LinuxGameProcess::state() const noexcept { return impl_->state; }
std::optional<ProcessExit> LinuxGameProcess::exit_result() const noexcept { return impl_->result; }
ProcessDiagnostics LinuxGameProcess::diagnostics() const noexcept { return impl_->metrics; }

void LinuxGameProcess::launch(const GameProfile &profile) {
    auto &p = *impl_;
    if (p.state != ProcessState::Idle)
        throw std::logic_error("Game process instances are single-use");
    p.state = ProcessState::Starting;
    ++p.metrics.launch_attempts;
    try {
        if (validate(profile))
            throw GameProcessError(ProcessFailure::InvalidProfile);
        std::error_code error;
        if (!std::filesystem::is_regular_file(profile.executable, error) ||
            access(profile.executable.c_str(), X_OK) != 0)
            throw GameProcessError(ProcessFailure::ExecutableUnavailable);
        if (!std::filesystem::is_directory(profile.working_directory, error))
            throw GameProcessError(ProcessFailure::DirectoryUnavailable);
        std::vector<const char *> arguments;
        arguments.reserve(profile.arguments.size() + 2);
        arguments.push_back(profile.executable.c_str());
        for (const auto &argument : profile.arguments)
            arguments.push_back(argument.c_str());
        arguments.push_back(nullptr);
        const auto flags = static_cast<GSubprocessFlags>(G_SUBPROCESS_FLAGS_STDOUT_SILENCE |
                                                         G_SUBPROCESS_FLAGS_STDERR_SILENCE);
        auto *launcher = g_subprocess_launcher_new(flags);
        g_subprocess_launcher_set_cwd(launcher, profile.working_directory.c_str());
        p.child = g_subprocess_launcher_spawnv(launcher, arguments.data(), nullptr);
        g_object_unref(launcher);
        if (!p.child)
            throw GameProcessError(ProcessFailure::Spawn);
        p.state = ProcessState::Running;
    } catch (...) {
        p.state = ProcessState::Failed;
        ++p.metrics.failures;
        throw;
    }
}

void LinuxGameProcess::poll() {
    auto &p = *impl_;
    if (p.state != ProcessState::Running && p.state != ProcessState::StopRequested)
        return;
    // The identifier becomes null only after GIO has recorded child termination.
    if (g_subprocess_get_identifier(p.child))
        return;
    if (!g_subprocess_wait(p.child, nullptr, nullptr)) {
        p.state = ProcessState::Failed;
        ++p.metrics.failures;
        throw GameProcessError(ProcessFailure::Wait);
    }
    ProcessExit result;
    if (g_subprocess_get_if_exited(p.child))
        result.exit_code = g_subprocess_get_exit_status(p.child);
    else if (g_subprocess_get_if_signaled(p.child))
        result.signal = g_subprocess_get_term_sig(p.child);
    p.result = result;
    p.state = ProcessState::Exited;
}

bool LinuxGameProcess::request_stop(StopMode mode) {
    if (mode != StopMode::Graceful && mode != StopMode::Force)
        throw std::invalid_argument("Invalid game stop mode");
    poll();
    auto &p = *impl_;
    if (p.state != ProcessState::Running && p.state != ProcessState::StopRequested)
        return false;
    if (p.stop_mode == StopMode::Force || p.stop_mode == mode)
        return true;
    if (mode == StopMode::Force)
        g_subprocess_force_exit(p.child);
    else
        g_subprocess_send_signal(p.child, SIGTERM);
    p.stop_mode = mode;
    p.state = ProcessState::StopRequested;
    ++p.metrics.stop_requests;
    return true;
}

bool LinuxGameProcess::detach() {
    poll();
    auto &p = *impl_;
    if (p.state != ProcessState::Running && p.state != ProcessState::StopRequested)
        return false;
    g_object_unref(p.child);
    p.child = nullptr;
    p.state = ProcessState::Detached;
    return true;
}
} // namespace cloudplay::games

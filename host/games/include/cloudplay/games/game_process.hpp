#pragma once

#include <cloudplay/games/game_profile.hpp>
#include <cstdint>
#include <optional>
#include <stdexcept>

namespace cloudplay::games {

enum class ProcessState { Idle, Starting, Running, StopRequested, Exited, Failed, Detached };
enum class StopMode { Graceful, Force };
enum class ProcessFailure {
    InvalidProfile,
    ExecutableUnavailable,
    DirectoryUnavailable,
    Spawn,
    Wait
};
class GameProcessError final : public std::runtime_error {
  public:
    explicit GameProcessError(ProcessFailure failure)
        : std::runtime_error("Game process operation failed"), reason(failure) {}
    const ProcessFailure reason;
};
struct ProcessExit {
    std::optional<int> exit_code;
    std::optional<int> signal;
};
struct ProcessDiagnostics {
    std::uint64_t launch_attempts{}, stop_requests{}, failures{};
};

// Owner-thread only. Orchestration must authorize local profile selection first.
class IGameProcess {
  public:
    virtual ~IGameProcess() = default;
    virtual void launch(const GameProfile &profile) = 0;
    virtual void poll() = 0;
    // No automatic escalation: Force requires a separate explicit user request.
    virtual bool request_stop(StopMode mode = StopMode::Graceful) = 0;
    virtual bool detach() = 0;
    [[nodiscard]] virtual ProcessState state() const noexcept = 0;
    [[nodiscard]] virtual std::optional<ProcessExit> exit_result() const noexcept = 0;
    [[nodiscard]] virtual ProcessDiagnostics diagnostics() const noexcept = 0;
};

} // namespace cloudplay::games

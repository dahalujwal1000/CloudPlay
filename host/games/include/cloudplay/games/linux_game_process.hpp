#pragma once

#include <cloudplay/games/game_process.hpp>
#include <memory>

namespace cloudplay::games {

// Tracks only the direct child, not daemonized launchers or descendant processes.
// Destruction releases monitoring; it never implies permission to kill a game.
class LinuxGameProcess final : public IGameProcess {
  public:
    LinuxGameProcess();
    ~LinuxGameProcess() override;
    LinuxGameProcess(const LinuxGameProcess &) = delete;
    LinuxGameProcess &operator=(const LinuxGameProcess &) = delete;
    void launch(const GameProfile &profile) override;
    void poll() override;
    bool request_stop(StopMode mode = StopMode::Graceful) override;
    bool detach() override;
    [[nodiscard]] ProcessState state() const noexcept override;
    [[nodiscard]] std::optional<ProcessExit> exit_result() const noexcept override;
    [[nodiscard]] ProcessDiagnostics diagnostics() const noexcept override;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace cloudplay::games

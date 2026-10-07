#pragma once

#include <cloudplay/games/game_process.hpp>
#include <cloudplay/games/local_profiles.hpp>
#include <memory>

namespace cloudplay::app {

// Owner-thread orchestration, constructed only after independent authorization.
// Launch accepts an approved snapshot ID, never peer-supplied executable details.
class GameSession final {
  public:
    GameSession(games::LocalProfileCatalog profiles, std::unique_ptr<games::IGameProcess> process);
    GameSession(const GameSession &) = delete;
    GameSession &operator=(const GameSession &) = delete;
    void launch(std::string_view approved_id);
    void poll();
    bool request_stop(games::StopMode mode = games::StopMode::Graceful);
    bool detach();
    [[nodiscard]] games::ProcessState state() const noexcept;
    [[nodiscard]] std::optional<games::ProcessExit> exit_result() const noexcept;
    [[nodiscard]] games::ProcessDiagnostics diagnostics() const noexcept;

  private:
    games::LocalProfileCatalog profiles_;
    std::unique_ptr<games::IGameProcess> process_;
};
} // namespace cloudplay::app

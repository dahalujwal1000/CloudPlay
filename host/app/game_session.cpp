#include <cloudplay/app/game_session.hpp>
#include <utility>

namespace cloudplay::app {
GameSession::GameSession(games::LocalProfileCatalog profiles,
                         std::unique_ptr<games::IGameProcess> process)
    : profiles_(std::move(profiles)), process_(std::move(process)) {
    if (!process_)
        throw std::invalid_argument("Game session requires a process backend");
}
void GameSession::launch(std::string_view approved_id) {
    if (process_->state() != games::ProcessState::Idle)
        throw std::logic_error("Game session is single-use");
    const auto *profile = profiles_.find(approved_id);
    if (!profile)
        throw std::invalid_argument("Game profile ID is not approved");
    process_->launch(*profile);
}
void GameSession::poll() { process_->poll(); }
bool GameSession::request_stop(games::StopMode mode) { return process_->request_stop(mode); }
bool GameSession::detach() { return process_->detach(); }
games::ProcessState GameSession::state() const noexcept { return process_->state(); }
std::optional<games::ProcessExit> GameSession::exit_result() const noexcept {
    return process_->exit_result();
}
games::ProcessDiagnostics GameSession::diagnostics() const noexcept {
    return process_->diagnostics();
}
} // namespace cloudplay::app

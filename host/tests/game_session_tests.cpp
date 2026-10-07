#include <array>
#include <cloudplay/app/game_session.hpp>
#include <type_traits>

namespace {
using namespace cloudplay::games;
void check(bool condition) {
    if (!condition)
        throw std::runtime_error("Game session assertion failed");
}
struct Record {
    unsigned launches{}, stops{}, detached{}, destroyed{};
    std::string id;
};
class Process final : public IGameProcess {
  public:
    explicit Process(Record &record) : record_(record) {}
    ~Process() override { ++record_.destroyed; }
    void launch(const GameProfile &profile) override {
        ++record_.launches;
        record_.id = profile.id;
        state_ = ProcessState::Running;
    }
    void poll() override {}
    bool request_stop(StopMode) override {
        ++record_.stops;
        state_ = ProcessState::StopRequested;
        return true;
    }
    bool detach() override {
        ++record_.detached;
        state_ = ProcessState::Detached;
        return true;
    }
    ProcessState state() const noexcept override { return state_; }
    std::optional<ProcessExit> exit_result() const noexcept override { return std::nullopt; }
    ProcessDiagnostics diagnostics() const noexcept override {
        return {record_.launches, record_.stops, 0};
    }

  private:
    Record &record_;
    ProcessState state_{ProcessState::Idle};
};
} // namespace
int main(int argc, char **argv) {
    using cloudplay::app::GameSession;
    check(argc == 2);
    static_assert(!std::is_copy_constructible_v<GameSession>);
    const auto catalog = LocalProfileCatalog::load(std::array{std::filesystem::path(argv[1])});
    Record record;
    {
        GameSession game(catalog, std::make_unique<Process>(record));
        try {
            game.launch("unapproved");
            return 1;
        } catch (const std::invalid_argument &) {
        }
        check(record.launches == 0 && game.state() == ProcessState::Idle);
        game.launch("game-check");
        check(record.id == "game-check" && record.launches == 1 &&
              game.state() == ProcessState::Running);
        try {
            game.launch("game-check");
            return 1;
        } catch (const std::logic_error &) {
        }
        game.poll();
        check(!game.exit_result());
        check(game.request_stop() && game.state() == ProcessState::StopRequested);
        check(game.diagnostics().stop_requests == 1);
        check(game.detach() && game.state() == ProcessState::Detached);
    }
    check(record.destroyed == 1 && record.stops == 1 && record.detached == 1);
    try {
        GameSession invalid(catalog, nullptr);
        return 1;
    } catch (const std::invalid_argument &) {
    }
    Record destructor;
    {
        GameSession game(catalog, std::make_unique<Process>(destructor));
        game.launch("game-check");
    }
    check(destructor.destroyed == 1 && destructor.stops == 0);
}

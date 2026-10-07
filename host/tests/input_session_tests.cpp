#include <array>
#include <cloudplay/app/input_session.hpp>
#include <stdexcept>
#include <type_traits>

namespace {
using namespace cloudplay;
using namespace cloudplay::input;
void check(bool condition) {
    if (!condition)
        throw std::runtime_error("Input session assertion failed");
}
struct Record {
    unsigned presses{}, releases{}, destroyed{};
    bool fail_release{};
};
class Sink final : public IInputSink {
  public:
    explicit Sink(Record &record) : record_(record) {}
    ~Sink() override { ++record_.destroyed; }
    bool apply(const Payload &payload) noexcept override {
        if (const auto *key = std::get_if<KeyAction>(&payload)) {
            if (key->pressed)
                ++record_.presses;
            else {
                ++record_.releases;
                return !record_.fail_release;
            }
        }
        return true;
    }

  private:
    Record &record_;
};
std::array<std::uint8_t, 34> press(std::uint8_t sequence = 1, std::uint8_t session = 1) {
    std::array<std::uint8_t, 34> bytes{};
    bytes[0] = 'C';
    bytes[1] = 'P';
    bytes[2] = 'I';
    bytes[3] = 'N';
    bytes[4] = bytes[5] = 1;
    bytes[7] = 2;
    bytes[15] = sequence;
    bytes[23] = 1;
    bytes[31] = session;
    bytes[33] = 0x1a;
    return bytes;
}
Session connected() {
    Session core;
    for (const auto event : {SessionEvent::Start, SessionEvent::Initialized, SessionEvent::Connect,
                             SessionEvent::PeerConnected})
        check(core.apply(event).has_value());
    return core;
}
void accepted(app::InputSession &session, const std::array<std::uint8_t, 34> &packet) {
    check(std::get<EnqueueStatus>(session.receive(packet)) == EnqueueStatus::Accepted);
}
} // namespace

int main() {
    static_assert(!std::is_copy_constructible_v<app::InputSession>);
    static_assert(!std::is_move_constructible_v<app::InputSession>);
    for (const auto event : {SessionEvent::Disconnect, SessionEvent::Stop, SessionEvent::Fail,
                             SessionEvent::GameStopped}) {
        Record record;
        {
            app::InputSession input(1, std::make_unique<Sink>(record));
            auto core = connected();
            const auto launch = core.apply(SessionEvent::LaunchGame);
            check(launch && input.on_transition(*launch));
            check(core.apply(SessionEvent::StreamReady).has_value());
            accepted(input, press());
            check(input.drain() == 1);
            accepted(input, press(2));
            const auto transition = core.apply(event, event == SessionEvent::Fail
                                                          ? std::optional{SessionFailure::Network}
                                                          : std::nullopt);
            check(transition && input.on_transition(*transition));
            check(input.state() == DispatchState::Closed && input.drain() == 0);
            check(record.presses == 1 && record.releases == 1 && record.destroyed == 0);
            check(input.diagnostics().discarded_pending == 1);
            check(std::get<EnqueueStatus>(input.receive(press(3))) == EnqueueStatus::Closed);
            check(input.close() && record.releases == 1);
        }
        check(record.destroyed == 1 && record.releases == 1);
    }
    Record retry;
    {
        app::InputSession input(1, std::make_unique<Sink>(retry));
        accepted(input, press());
        check(input.drain() == 1);
        retry.fail_release = true;
        auto core = connected();
        const auto transition = core.apply(SessionEvent::Disconnect);
        check(transition && !input.on_transition(*transition));
        check(input.state() == DispatchState::CleanupFailed && retry.destroyed == 0);
        check(!input.close());
        retry.fail_release = false;
        check(input.close() && input.state() == DispatchState::Closed);
    }
    Record destructor;
    {
        app::InputSession input(1, std::make_unique<Sink>(destructor));
        accepted(input, press());
        check(input.drain() == 1);
    }
    check(destructor.releases == 1 && destructor.destroyed == 1);
    Record fresh;
    {
        app::InputSession input(2, std::make_unique<Sink>(fresh));
        check(std::get<DecodeError>(input.receive(press())) == DecodeError::Session);
        accepted(input, press(1, 2));
        check(std::get<DecodeError>(input.receive(press(1, 2))) == DecodeError::Replay);
    }
    try {
        app::InputSession invalid(1, nullptr);
        return 1;
    } catch (const std::invalid_argument &) {
    }
    Record invalid_record;
    try {
        app::InputSession invalid(0, std::make_unique<Sink>(invalid_record));
        return 1;
    } catch (const std::invalid_argument &) {
    }
    check(invalid_record.destroyed == 1);
}

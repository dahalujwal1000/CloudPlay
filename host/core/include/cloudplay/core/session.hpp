#pragma once

#include <optional>
#include <string_view>

namespace cloudplay {

enum class SessionState {
    Offline,
    Starting,
    Ready,
    Pairing,
    Connecting,
    Connected,
    StartingGame,
    Streaming,
    Stopping
};

enum class SessionEvent {
    Start,
    Initialized,
    Pair,
    Paired,
    Connect,
    PeerConnected,
    LaunchGame,
    StreamReady,
    GameStopped,
    Disconnect,
    Fail,
    Stop,
    Stopped
};

enum class SessionFailure { Auth, Network, GameStart, Capture, Encoder, WebRtc, Input };

std::string_view name(SessionState state) noexcept;
std::string_view name(SessionEvent event) noexcept;
std::string_view name(SessionFailure failure) noexcept;
std::optional<SessionState> next_state(SessionState state, SessionEvent event) noexcept;

struct Transition {
    SessionState from;
    SessionState to;
    SessionEvent event;
    std::optional<SessionFailure> failure;
};

// Owned by the orchestration thread. Callers must serialize transitions.
class Session {
  public:
    [[nodiscard]] SessionState state() const noexcept { return state_; }
    [[nodiscard]] std::optional<Transition>
    apply(SessionEvent event, std::optional<SessionFailure> failure = std::nullopt) noexcept;

  private:
    SessionState state_{SessionState::Offline};
};

} // namespace cloudplay

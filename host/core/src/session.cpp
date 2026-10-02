#include <cloudplay/core/session.hpp>

namespace cloudplay {

std::string_view name(SessionState state) noexcept {
    switch (state) {
    case SessionState::Offline:
        return "OFFLINE";
    case SessionState::Starting:
        return "STARTING";
    case SessionState::Ready:
        return "READY";
    case SessionState::Pairing:
        return "PAIRING";
    case SessionState::Connecting:
        return "CONNECTING";
    case SessionState::Connected:
        return "CONNECTED";
    case SessionState::StartingGame:
        return "STARTING_GAME";
    case SessionState::Streaming:
        return "STREAMING";
    case SessionState::Stopping:
        return "STOPPING";
    }
    return "UNKNOWN";
}

std::string_view name(SessionEvent event) noexcept {
    switch (event) {
    case SessionEvent::Start:
        return "START";
    case SessionEvent::Initialized:
        return "INITIALIZED";
    case SessionEvent::Pair:
        return "PAIR";
    case SessionEvent::Paired:
        return "PAIRED";
    case SessionEvent::Connect:
        return "CONNECT";
    case SessionEvent::PeerConnected:
        return "PEER_CONNECTED";
    case SessionEvent::LaunchGame:
        return "LAUNCH_GAME";
    case SessionEvent::StreamReady:
        return "STREAM_READY";
    case SessionEvent::GameStopped:
        return "GAME_STOPPED";
    case SessionEvent::Disconnect:
        return "DISCONNECT";
    case SessionEvent::Fail:
        return "FAIL";
    case SessionEvent::Stop:
        return "STOP";
    case SessionEvent::Stopped:
        return "STOPPED";
    }
    return "UNKNOWN";
}

std::string_view name(SessionFailure failure) noexcept {
    switch (failure) {
    case SessionFailure::Auth:
        return "AUTH_FAILED";
    case SessionFailure::Network:
        return "NETWORK_FAILED";
    case SessionFailure::GameStart:
        return "GAME_START_FAILED";
    case SessionFailure::Capture:
        return "CAPTURE_FAILED";
    case SessionFailure::Encoder:
        return "ENCODER_FAILED";
    case SessionFailure::WebRtc:
        return "WEBRTC_FAILED";
    case SessionFailure::Input:
        return "INPUT_FAILED";
    }
    return "UNKNOWN";
}

std::optional<SessionState> next_state(SessionState state, SessionEvent event) noexcept {
    if (event == SessionEvent::Stop && state != SessionState::Offline &&
        state != SessionState::Stopping) {
        return SessionState::Stopping;
    }
    if (event == SessionEvent::Fail && state != SessionState::Offline &&
        state != SessionState::Stopping) {
        // Failure recovery requires resource cleanup before returning to OFFLINE.
        return SessionState::Stopping;
    }
    switch (state) {
    case SessionState::Offline:
        if (event == SessionEvent::Start)
            return SessionState::Starting;
        break;
    case SessionState::Starting:
        if (event == SessionEvent::Initialized)
            return SessionState::Ready;
        break;
    case SessionState::Ready:
        if (event == SessionEvent::Pair)
            return SessionState::Pairing;
        if (event == SessionEvent::Connect)
            return SessionState::Connecting;
        break;
    case SessionState::Pairing:
        if (event == SessionEvent::Paired || event == SessionEvent::Disconnect)
            return SessionState::Ready;
        break;
    case SessionState::Connecting:
        if (event == SessionEvent::PeerConnected)
            return SessionState::Connected;
        if (event == SessionEvent::Disconnect)
            return SessionState::Stopping;
        break;
    case SessionState::Connected:
        if (event == SessionEvent::LaunchGame)
            return SessionState::StartingGame;
        if (event == SessionEvent::Disconnect)
            return SessionState::Stopping;
        break;
    case SessionState::StartingGame:
        if (event == SessionEvent::StreamReady)
            return SessionState::Streaming;
        if (event == SessionEvent::GameStopped)
            return SessionState::Connected;
        if (event == SessionEvent::Disconnect)
            return SessionState::Stopping;
        break;
    case SessionState::Streaming:
        if (event == SessionEvent::GameStopped)
            return SessionState::Connected;
        if (event == SessionEvent::Disconnect)
            return SessionState::Stopping;
        break;
    case SessionState::Stopping:
        if (event == SessionEvent::Stopped)
            return SessionState::Offline;
        break;
    }
    return std::nullopt;
}

std::optional<Transition> Session::apply(SessionEvent event,
                                         std::optional<SessionFailure> failure) noexcept {
    if ((event == SessionEvent::Fail) != failure.has_value())
        return std::nullopt;
    const auto next = next_state(state_, event);
    if (!next)
        return std::nullopt;
    const Transition transition{state_, *next, event, failure};
    state_ = *next;
    return transition;
}

} // namespace cloudplay

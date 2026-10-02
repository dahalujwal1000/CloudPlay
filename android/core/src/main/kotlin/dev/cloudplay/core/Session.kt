package dev.cloudplay.core

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow

enum class SessionState {
    OFFLINE,
    STARTING,
    READY,
    PAIRING,
    CONNECTING,
    CONNECTED,
    STARTING_GAME,
    STREAMING,
    STOPPING,
}

enum class SessionEvent {
    START,
    INITIALIZED,
    PAIR,
    PAIRED,
    CONNECT,
    PEER_CONNECTED,
    LAUNCH_GAME,
    STREAM_READY,
    GAME_STOPPED,
    DISCONNECT,
    FAIL,
    STOP,
    STOPPED,
}

enum class SessionFailure {
    AUTH_FAILED,
    NETWORK_FAILED,
    GAME_START_FAILED,
    CAPTURE_FAILED,
    ENCODER_FAILED,
    WEBRTC_FAILED,
    INPUT_FAILED,
}

data class Transition(
    val from: SessionState,
    val to: SessionState,
    val event: SessionEvent,
    val failure: SessionFailure?,
)

// One orchestration owner must serialize mutations; UI observers receive a read-only flow.
class Session {
    private val mutableState = MutableStateFlow(SessionState.OFFLINE)
    val state = mutableState.asStateFlow()

    fun apply(
        event: SessionEvent,
        failure: SessionFailure? = null,
    ): Transition? {
        if ((event == SessionEvent.FAIL) != (failure != null)) return null
        val from = state.value
        val to = nextState(from, event) ?: return null
        mutableState.value = to
        return Transition(from, to, event, failure)
    }
}

fun nextState(
    state: SessionState,
    event: SessionEvent,
): SessionState? {
    if (event == SessionEvent.STOP || event == SessionEvent.FAIL) {
        return if (state != SessionState.OFFLINE && state != SessionState.STOPPING) {
            SessionState.STOPPING
        } else {
            null
        }
    }
    return when (state) {
        SessionState.OFFLINE -> if (event == SessionEvent.START) SessionState.STARTING else null
        SessionState.STARTING -> if (event == SessionEvent.INITIALIZED) SessionState.READY else null
        SessionState.READY ->
            when (event) {
                SessionEvent.PAIR -> SessionState.PAIRING
                SessionEvent.CONNECT -> SessionState.CONNECTING
                else -> null
            }
        SessionState.PAIRING ->
            when (event) {
                SessionEvent.PAIRED, SessionEvent.DISCONNECT -> SessionState.READY
                else -> null
            }
        SessionState.CONNECTING ->
            when (event) {
                SessionEvent.PEER_CONNECTED -> SessionState.CONNECTED
                SessionEvent.DISCONNECT -> SessionState.STOPPING
                else -> null
            }
        SessionState.CONNECTED ->
            when (event) {
                SessionEvent.LAUNCH_GAME -> SessionState.STARTING_GAME
                SessionEvent.DISCONNECT -> SessionState.STOPPING
                else -> null
            }
        SessionState.STARTING_GAME, SessionState.STREAMING ->
            when (event) {
                SessionEvent.STREAM_READY -> if (state == SessionState.STARTING_GAME) SessionState.STREAMING else null
                SessionEvent.GAME_STOPPED -> SessionState.CONNECTED
                SessionEvent.DISCONNECT -> SessionState.STOPPING
                else -> null
            }
        SessionState.STOPPING -> if (event == SessionEvent.STOPPED) SessionState.OFFLINE else null
    }
}

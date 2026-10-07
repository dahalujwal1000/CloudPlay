package dev.cloudplay.core

enum class HostConnectionState { DISCONNECTED, PAIRING, CHECKING, CONNECTED, FAILED }

data class HostConnectionStatus(
    val state: HostConnectionState = HostConnectionState.DISCONNECTED,
    val host: String = "",
    val error: String? = null,
)

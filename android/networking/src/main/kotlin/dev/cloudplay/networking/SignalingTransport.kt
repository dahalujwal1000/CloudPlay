package dev.cloudplay.networking

import kotlinx.coroutines.flow.StateFlow
import java.net.URI

enum class ConnectionState { DISCONNECTED, CONNECTING, CONNECTED, CLOSING }

// Transport owns its sockets and jobs. Authentication implementation is a later task.
interface SignalingTransport {
    val state: StateFlow<ConnectionState>

    suspend fun connect(endpoint: URI)

    suspend fun disconnect()
}

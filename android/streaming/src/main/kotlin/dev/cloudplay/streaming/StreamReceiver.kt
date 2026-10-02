package dev.cloudplay.streaming

import dev.cloudplay.core.StreamConfig

// Implementations own decoder/media resources and release them before stop returns.
interface StreamReceiver {
    suspend fun start(config: StreamConfig)

    suspend fun stop()
}

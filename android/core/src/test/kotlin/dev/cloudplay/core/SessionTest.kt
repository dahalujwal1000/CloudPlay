package dev.cloudplay.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Test

class SessionTest {
    @Test
    fun lifecycleAndRecovery() {
        val session = Session()
        assertNull(session.apply(SessionEvent.STREAM_READY))
        val events =
            listOf(
                SessionEvent.START,
                SessionEvent.INITIALIZED,
                SessionEvent.PAIR,
                SessionEvent.PAIRED,
                SessionEvent.CONNECT,
                SessionEvent.PEER_CONNECTED,
                SessionEvent.LAUNCH_GAME,
                SessionEvent.STREAM_READY,
            )
        events.forEach { assertNotNull(session.apply(it)) }
        assertEquals(SessionState.STREAMING, session.state.value)
        assertNull(session.apply(SessionEvent.FAIL))
        assertNull(session.apply(SessionEvent.STOP, SessionFailure.NETWORK_FAILED))
        assertEquals(SessionState.STREAMING, session.state.value)
        assertNotNull(session.apply(SessionEvent.FAIL, SessionFailure.NETWORK_FAILED))
        assertEquals(SessionState.STOPPING, session.state.value)
        assertNull(session.apply(SessionEvent.CONNECT))
        assertNotNull(session.apply(SessionEvent.STOPPED))
        assertEquals(SessionState.OFFLINE, session.state.value)
    }

    @Test
    fun stopAndFailureRequireCleanup() {
        SessionState.entries
            .filter { it != SessionState.OFFLINE && it != SessionState.STOPPING }
            .forEach {
                assertEquals(SessionState.STOPPING, nextState(it, SessionEvent.STOP))
                assertEquals(SessionState.STOPPING, nextState(it, SessionEvent.FAIL))
            }
        assertEquals(SessionState.CONNECTED, nextState(SessionState.STREAMING, SessionEvent.GAME_STOPPED))
    }

    @Test
    fun supportedBitrates() {
        listOf(6, 12, 20).forEach { assertEquals(it, StreamConfig(bitrateMbps = it).bitrateMbps) }
    }

    @Test(expected = IllegalArgumentException::class)
    fun rejectsInvalidBitrate() {
        StreamConfig(bitrateMbps = 21)
    }
}

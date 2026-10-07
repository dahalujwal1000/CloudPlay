package dev.cloudplay.app

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import dev.cloudplay.core.HostConnectionState
import dev.cloudplay.core.HostConnectionStatus
import dev.cloudplay.networking.HostTrust
import dev.cloudplay.networking.PairingClient
import dev.cloudplay.networking.PairingFailure
import dev.cloudplay.ui.CloudPlayScreen
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Job
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.launch
import javax.net.ssl.SSLException

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContent {
            val scope = rememberCoroutineScope()
            var status by remember { mutableStateOf(HostConnectionStatus()) }
            var client by remember { mutableStateOf<PairingClient?>(null) }
            var operation by remember { mutableStateOf<Job?>(null) }

            fun disconnect() {
                operation?.cancel()
                client?.forget()
                client = null
                status = HostConnectionStatus()
            }

            fun start(
                state: HostConnectionState,
                host: String,
                action: suspend () -> Unit,
            ) {
                if (operation?.isActive == true) return
                status = HostConnectionStatus(state, host)
                operation =
                    scope.launch {
                        try {
                            action()
                            ensureActive()
                            status = HostConnectionStatus(HostConnectionState.CONNECTED, host)
                        } catch (error: CancellationException) {
                            throw error
                        } catch (error: Exception) {
                            ensureActive()
                            client?.forget()
                            client = null
                            val message =
                                when (error) {
                                    is SSLException -> "Certificate verification failed."
                                    is IllegalArgumentException -> "Check the host, port, fingerprint, and pairing details."
                                    is PairingFailure ->
                                        when (error.reason) {
                                            PairingFailure.Reason.REJECTED -> "Pairing or device authorization was rejected."
                                            PairingFailure.Reason.RATE_LIMITED -> "Too many attempts. Try again later."
                                            PairingFailure.Reason.EXPIRED -> "Device authorization expired. Pair again."
                                            PairingFailure.Reason.RESPONSE -> "Unexpected host response."
                                        }
                                    else -> "Host connection failed."
                                }
                            status = HostConnectionStatus(HostConnectionState.FAILED, host, message)
                        }
                    }
            }
            DisposableEffect(Unit) {
                onDispose {
                    operation?.cancel()
                    client?.forget()
                }
            }
            CloudPlayScreen(
                status = status,
                onPair = { host, port, fingerprint, challenge, code ->
                    start(HostConnectionState.PAIRING, host) {
                        client?.forget()
                        val next = PairingClient(HostTrust(host, port.toInt(), fingerprint))
                        client = next
                        next.pair(challenge, code)
                        next.checkStatus()
                    }
                },
                onCheck = {
                    start(HostConnectionState.CHECKING, status.host) {
                        client?.checkStatus()
                            ?: throw PairingFailure(PairingFailure.Reason.EXPIRED)
                    }
                },
                onDisconnect = { disconnect() },
                modifier = Modifier.windowInsetsPadding(WindowInsets.safeDrawing),
            )
        }
    }
}

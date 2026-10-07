package dev.cloudplay.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.Checkbox
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp
import dev.cloudplay.core.HostConnectionState
import dev.cloudplay.core.HostConnectionStatus

@Composable
@Suppress("FunctionName")
fun CloudPlayScreen(
    status: HostConnectionStatus,
    onPair: (String, String, String, String, String) -> Unit,
    onCheck: () -> Unit,
    onDisconnect: () -> Unit,
    modifier: Modifier = Modifier,
) {
    var host by remember { mutableStateOf("") }
    var port by remember { mutableStateOf("8787") }
    var fingerprint by remember { mutableStateOf("") }
    var challenge by remember { mutableStateOf("") }
    var code by remember { mutableStateOf("") }
    var confirmed by remember { mutableStateOf(false) }
    val busy = status.state == HostConnectionState.PAIRING || status.state == HostConnectionState.CHECKING
    val paired = status.state == HostConnectionState.CONNECTED || status.state == HostConnectionState.CHECKING
    MaterialTheme {
        Surface(modifier = modifier.fillMaxSize()) {
            Column(
                modifier =
                    Modifier
                        .fillMaxSize()
                        .imePadding()
                        .verticalScroll(rememberScrollState())
                        .padding(24.dp),
                verticalArrangement = Arrangement.spacedBy(16.dp),
            ) {
                Text("CloudPlay", style = MaterialTheme.typography.headlineMedium)
                Text("Host connection", style = MaterialTheme.typography.titleMedium)
                Text(
                    when (status.state) {
                        HostConnectionState.DISCONNECTED -> "Not connected"
                        HostConnectionState.PAIRING -> "Pairing"
                        HostConnectionState.CHECKING -> "Checking host"
                        HostConnectionState.CONNECTED -> "Host authenticated"
                        HostConnectionState.FAILED -> "Connection failed"
                    },
                )
                HorizontalDivider()
                status.error?.let { Text(it, color = MaterialTheme.colorScheme.error) }
                if (paired) {
                    Text(status.host, style = MaterialTheme.typography.titleMedium)
                    Button(onClick = onCheck, enabled = !busy, modifier = Modifier.fillMaxWidth()) { Text("Check status") }
                } else {
                    OutlinedTextField(host, {
                        host = it.take(15)
                        confirmed = false
                    }, label = { Text("Host IPv4 address") }, singleLine = true, enabled = !busy, modifier = Modifier.fillMaxWidth())
                    OutlinedTextField(
                        port,
                        {
                            port = it.take(5)
                            confirmed = false
                        },
                        label = {
                            Text("Port")
                        },
                        singleLine = true,
                        keyboardOptions =
                            KeyboardOptions(
                                keyboardType = KeyboardType.Number,
                            ),
                        enabled = !busy,
                        modifier = Modifier.fillMaxWidth(),
                    )
                    OutlinedTextField(fingerprint, {
                        fingerprint = it.take(95)
                        confirmed = false
                    }, label = {
                        Text(
                            "Certificate SHA-256",
                        )
                    }, minLines = 2, maxLines = 4, enabled = !busy, modifier = Modifier.fillMaxWidth())
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Checkbox(checked = confirmed, onCheckedChange = { confirmed = it }, enabled = !busy)
                        Text("Fingerprint confirmed on PC", modifier = Modifier.weight(1f))
                    }
                    OutlinedTextField(challenge, {
                        challenge = it.take(36)
                    }, label = { Text("Challenge ID") }, singleLine = true, enabled = !busy, modifier = Modifier.fillMaxWidth())
                    OutlinedTextField(
                        code,
                        {
                            code = it.take(8)
                        },
                        label = {
                            Text("Pairing code")
                        },
                        singleLine = true,
                        visualTransformation = PasswordVisualTransformation(),
                        keyboardOptions =
                            KeyboardOptions(
                                keyboardType = KeyboardType.NumberPassword,
                            ),
                        enabled = !busy,
                        modifier = Modifier.fillMaxWidth(),
                    )
                    Button(
                        onClick = {
                            onPair(host, port, fingerprint, challenge, code)
                            code = ""
                        },
                        enabled =
                            !busy && confirmed && host.isNotBlank() && challenge.length == 36 && code.length == 8,
                        modifier = Modifier.fillMaxWidth(),
                    ) { Text("Pair host") }
                }
                if (busy) CircularProgressIndicator()
                if (busy || paired || status.state == HostConnectionState.FAILED) {
                    TextButton(onClick = {
                        code = ""
                        onDisconnect()
                    }, modifier = Modifier.fillMaxWidth()) { Text(if (busy) "Cancel" else "Disconnect") }
                }
            }
        }
    }
}

package dev.cloudplay.ui

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp

@Composable
@Suppress("FunctionName")
fun CloudPlayScreen(modifier: Modifier = Modifier) {
    MaterialTheme {
        Surface(modifier = modifier.fillMaxSize()) {
            Column(modifier = Modifier.padding(24.dp)) {
                Text("CloudPlay", style = MaterialTheme.typography.headlineMedium)
                Spacer(Modifier.height(24.dp))
                Text("Devices", style = MaterialTheme.typography.titleMedium)
                Spacer(Modifier.height(12.dp))
                Text("No paired hosts", style = MaterialTheme.typography.bodyMedium)
            }
        }
    }
}

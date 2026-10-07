package dev.cloudplay.networking

import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.withContext
import org.json.JSONObject
import java.io.ByteArrayOutputStream
import java.net.URI
import java.security.KeyStore
import java.security.MessageDigest
import java.security.cert.CertificateException
import java.security.cert.X509Certificate
import java.util.UUID
import javax.net.ssl.HttpsURLConnection
import javax.net.ssl.SSLContext
import javax.net.ssl.TrustManagerFactory
import javax.net.ssl.X509TrustManager

class HostTrust(
    val host: String,
    val port: Int,
    fingerprint: String,
) {
    internal val pin: ByteArray

    init {
        val parts = host.split(".")
        require(parts.size == 4 && parts.all { it.matches(Regex("0|[1-9][0-9]{0,2}")) && it.toInt() <= 255 })
        val first = parts[0].toInt()
        val second = parts[1].toInt()
        require(host == "127.0.0.1" || first == 10 || (first == 192 && second == 168) || (first == 172 && second in 16..31))
        require(port in 1024..65535)
        require(fingerprint.matches(Regex("[A-Fa-f0-9]{64}|(?:[A-Fa-f0-9]{2}:){31}[A-Fa-f0-9]{2}")))
        pin =
            fingerprint
                .replace(":", "")
                .chunked(2)
                .map { it.toInt(16).toByte() }
                .toByteArray()
    }

    internal fun endpoint(path: String) = URI("https", null, host, port, path, null, null).toURL()
}

internal class PinnedTrustManager(
    private val pin: ByteArray,
) : X509TrustManager {
    override fun getAcceptedIssuers(): Array<X509Certificate> = emptyArray()

    override fun checkClientTrusted(
        chain: Array<X509Certificate>,
        authType: String,
    ): Unit = throw CertificateException("Client authentication unsupported")

    override fun checkServerTrusted(
        chain: Array<X509Certificate>,
        authType: String,
    ) {
        val leaf = chain.firstOrNull() ?: throw CertificateException("Missing certificate")
        if (!MessageDigest.isEqual(pin, MessageDigest.getInstance("SHA-256").digest(leaf.encoded))) {
            throw CertificateException("Certificate fingerprint mismatch")
        }
        leaf.checkValidity()
        val anchors =
            KeyStore.getInstance(KeyStore.getDefaultType()).apply {
                load(null)
                setCertificateEntry("confirmed-host", leaf)
            }
        val factory = TrustManagerFactory.getInstance(TrustManagerFactory.getDefaultAlgorithm()).apply { init(anchors) }
        factory.trustManagers
            .filterIsInstance<X509TrustManager>()
            .single()
            .checkServerTrusted(chain, authType)
    }
}

class PairingFailure(
    val reason: Reason,
) : Exception("Host connection failed") {
    enum class Reason { REJECTED, RATE_LIMITED, RESPONSE, EXPIRED }
}

// Single UI coroutine owner. Credentials are never exposed in UI state or persisted.
class PairingClient(
    private val trust: HostTrust,
) {
    private class Credential(
        val token: String,
        val expiresAt: Long,
    )

    private var credential: Credential? = null
    private var revision = 0L
    private val sockets =
        SSLContext
            .getInstance("TLS")
            .apply {
                init(null, arrayOf(PinnedTrustManager(trust.pin.copyOf())), null)
            }.socketFactory

    suspend fun pair(
        challenge: String,
        code: String,
    ) {
        require(challenge.matches(Regex("[0-9a-fA-F]{8}(?:-[0-9a-fA-F]{4}){3}-[0-9a-fA-F]{12}")))
        UUID.fromString(challenge)
        require(code.matches(Regex("[0-9]{8}")))
        credential = null
        val attempt = ++revision
        val body = JSONObject().put("challengeId", challenge).put("code", code).toString()
        val response = exchange("/v1/pairing/complete", body, null)
        val token = response.opt("token") as? String
        val expiry = response.opt("expiresAt") as? Long
        val device = response.opt("deviceId") as? String
        if (token == null || !token.matches(Regex("[A-Za-z0-9_-]{43}")) ||
            expiry == null || expiry <= System.currentTimeMillis() ||
            device == null || !device.matches(Regex("[0-9a-fA-F]{8}(?:-[0-9a-fA-F]{4}){3}-[0-9a-fA-F]{12}"))
        ) {
            throw PairingFailure(PairingFailure.Reason.RESPONSE)
        }
        if (attempt != revision) throw CancellationException("Connection was cleared")
        credential = Credential(token, expiry)
    }

    suspend fun checkStatus() {
        val attempt = revision
        val current = credential ?: throw PairingFailure(PairingFailure.Reason.EXPIRED)
        if (System.currentTimeMillis() >= current.expiresAt) {
            credential = null
            throw PairingFailure(PairingFailure.Reason.EXPIRED)
        }
        val response =
            try {
                exchange("/health", null, current.token)
            } catch (error: PairingFailure) {
                if (attempt == revision && error.reason == PairingFailure.Reason.REJECTED) forget()
                throw error
            }
        if (attempt != revision) throw CancellationException("Connection was cleared")
        if (System.currentTimeMillis() >= current.expiresAt) {
            forget()
            throw PairingFailure(PairingFailure.Reason.EXPIRED)
        }
        if (response.opt("status") != "ok" || response.opt("protocolVersion") != 1) {
            throw PairingFailure(PairingFailure.Reason.RESPONSE)
        }
    }

    fun forget() {
        ++revision
        credential = null
    }

    private suspend fun exchange(
        path: String,
        body: String?,
        token: String?,
    ): JSONObject =
        withContext(Dispatchers.IO) {
            ensureActive()
            val connection = trust.endpoint(path).openConnection() as HttpsURLConnection
            val deadline = System.nanoTime() + 15_000_000_000L
            try {
                connection.sslSocketFactory = sockets
                // Keep the platform hostname verifier; pinning does not replace IP/SAN checks.
                connection.instanceFollowRedirects = false
                connection.useCaches = false
                connection.connectTimeout = 5000
                connection.readTimeout = 5000
                connection.setRequestProperty("Accept", "application/json")
                if (token != null) connection.setRequestProperty("Authorization", "Bearer $token")
                if (body != null) {
                    connection.requestMethod = "POST"
                    connection.doOutput = true
                    val bytes = body.toByteArray(Charsets.UTF_8)
                    connection.setRequestProperty("Content-Type", "application/json")
                    connection.setFixedLengthStreamingMode(bytes.size)
                    connection.outputStream.use { it.write(bytes) }
                }
                when (connection.responseCode) {
                    200, 201 -> Unit
                    401, 403 -> throw PairingFailure(PairingFailure.Reason.REJECTED)
                    429 -> throw PairingFailure(PairingFailure.Reason.RATE_LIMITED)
                    else -> throw PairingFailure(PairingFailure.Reason.RESPONSE)
                }
                val bytes = ByteArrayOutputStream()
                connection.inputStream.use { stream ->
                    val buffer = ByteArray(1024)
                    while (true) {
                        ensureActive()
                        if (System.nanoTime() >= deadline) throw PairingFailure(PairingFailure.Reason.RESPONSE)
                        val count = stream.read(buffer)
                        if (count < 0) break
                        if (bytes.size() + count > 4096) throw PairingFailure(PairingFailure.Reason.RESPONSE)
                        bytes.write(buffer, 0, count)
                    }
                }
                ensureActive()
                JSONObject(bytes.toString("UTF-8"))
            } finally {
                connection.disconnect()
            }
        }
}

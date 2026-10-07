package dev.cloudplay.networking

import com.sun.net.httpserver.HttpsConfigurator
import com.sun.net.httpserver.HttpsServer
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.async
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withContext
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Test
import java.net.InetSocketAddress
import java.nio.file.Files
import java.security.KeyStore
import java.security.MessageDigest
import java.security.cert.X509Certificate
import java.util.UUID
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicInteger
import java.util.concurrent.atomic.AtomicReference
import javax.net.ssl.KeyManagerFactory
import javax.net.ssl.SSLContext
import javax.net.ssl.SSLException

class PairingClientTest {
    @Test
    fun endpointAndFingerprintValidation() {
        val pin = "ab".repeat(32)
        assertEquals("https://192.168.1.2:8787/health", HostTrust("192.168.1.2", 8787, pin).endpoint("/health").toString())
        for (host in listOf("8.8.8.8", "0.0.0.0", "example.com", "192.168.001.2", "192.168.1.999", "127.0.0.1/path", "172.32.0.1")) {
            assertThrows(IllegalArgumentException::class.java) { HostTrust(host, 8787, pin) }
        }
        for (invalid in listOf("", "ab".repeat(31), "zz".repeat(32), "a:".repeat(64))) {
            assertThrows(IllegalArgumentException::class.java) { HostTrust("127.0.0.1", 8787, invalid) }
        }
        assertThrows(IllegalArgumentException::class.java) { HostTrust("127.0.0.1", 0, pin) }
        assertThrows(IllegalArgumentException::class.java) { HostTrust("127.0.0.1", 65536, pin) }
    }

    @Test
    fun realTlsPairingTrustRejectionResponseBoundsAndForget() {
        val directory = Files.createTempDirectory("cloudplay-android-tls-")

        fun command(
            vararg args: String,
            input: String = "",
        ) {
            val process =
                ProcessBuilder(
                    *args,
                ).redirectOutput(ProcessBuilder.Redirect.DISCARD).redirectError(ProcessBuilder.Redirect.DISCARD).start()
            process.outputStream.use { it.write(input.toByteArray()) }
            if (!process.waitFor(10, TimeUnit.SECONDS)) {
                process.destroyForcibly().waitFor()
                fail("Fixture generation timed out")
            }
            assertEquals(0, process.exitValue())
        }
        var server: HttpsServer? = null
        try {
            val key = directory.resolve("key.pem").toString()
            val cert = directory.resolve("cert.pem").toString()
            val bundle = directory.resolve("identity.p12")
            command(
                "openssl",
                "req",
                "-x509",
                "-newkey",
                "rsa:2048",
                "-nodes",
                "-days",
                "1",
                "-subj",
                "/CN=CloudPlay Test",
                "-addext",
                "subjectAltName=IP:127.0.0.1",
                "-keyout",
                key,
                "-out",
                cert,
            )
            command(
                "openssl",
                "pkcs12",
                "-export",
                "-in",
                cert,
                "-inkey",
                key,
                "-out",
                bundle.toString(),
                "-passout",
                "stdin",
                input = "fixture-only\n",
            )
            val keys = KeyStore.getInstance("PKCS12").apply { Files.newInputStream(bundle).use { load(it, "fixture-only".toCharArray()) } }
            val manager =
                KeyManagerFactory
                    .getInstance(
                        KeyManagerFactory.getDefaultAlgorithm(),
                    ).apply { init(keys, "fixture-only".toCharArray()) }
            val context = SSLContext.getInstance("TLS").apply { init(manager.keyManagers, null, null) }
            val leaf = keys.getCertificate(keys.aliases().nextElement()) as X509Certificate
            val pin = MessageDigest.getInstance("SHA-256").digest(leaf.encoded).joinToString("") { "%02x".format(it) }
            val requests = AtomicInteger()
            val responseStatus = AtomicInteger(201)
            val largeResponse = AtomicBoolean(false)
            val redirect = AtomicBoolean(false)
            val delayResponse = AtomicBoolean(false)
            val pairingOverride = AtomicReference<String?>(null)
            val healthBody = AtomicReference("{\"status\":\"ok\",\"protocolVersion\":1}")
            val rejectHealth = AtomicBoolean(false)
            val delayHealth = AtomicBoolean(false)
            val healthStarted = CountDownLatch(1)
            val releaseHealth = CountDownLatch(1)
            val requestStarted = CountDownLatch(1)
            val releaseResponse = CountDownLatch(1)
            val token = "a".repeat(43)
            server =
                HttpsServer.create(InetSocketAddress("127.0.0.1", 0), 0).apply {
                    httpsConfigurator = HttpsConfigurator(context)
                    createContext("/v1/pairing/complete") { exchange ->
                        requests.incrementAndGet()
                        exchange.requestBody.close()
                        if (delayResponse.get()) {
                            requestStarted.countDown()
                            releaseResponse.await(3, TimeUnit.SECONDS)
                        }
                        if (redirect.get()) exchange.responseHeaders.add("Location", "/health")
                        val body =
                            pairingOverride.get() ?: if (largeResponse.get()) {
                                "x".repeat(4097)
                            } else {
                                JSONObject()
                                    .put("deviceId", UUID.randomUUID().toString())
                                    .put("token", token)
                                    .put(
                                        "expiresAt",
                                        System.currentTimeMillis() + 60000,
                                    ).toString()
                            }
                        val bytes = body.toByteArray()
                        exchange.sendResponseHeaders(if (redirect.get()) 302 else responseStatus.get(), bytes.size.toLong())
                        exchange.responseBody.use { it.write(bytes) }
                    }
                    createContext("/health") { exchange ->
                        requests.incrementAndGet()
                        if (delayHealth.get()) {
                            healthStarted.countDown()
                            releaseHealth.await(3, TimeUnit.SECONDS)
                        }
                        val authorized = !rejectHealth.get() && exchange.requestHeaders.getFirst("Authorization") == "Bearer $token"
                        val bytes = healthBody.get().toByteArray()
                        exchange.sendResponseHeaders(if (authorized) 200 else 401, bytes.size.toLong())
                        exchange.responseBody.use { it.write(bytes) }
                    }
                    start()
                }
            val client = PairingClient(HostTrust("127.0.0.1", server.address.port, pin))
            val challenge = UUID.randomUUID().toString()
            runBlocking {
                client.pair(challenge, "01234567")
                client.checkStatus()
            }
            for (version in listOf("\"1\"", "1.5", "true", "null")) {
                healthBody.set("{\"status\":\"ok\",\"protocolVersion\":$version}")
                val failure = assertThrows(PairingFailure::class.java) { runBlocking { client.checkStatus() } }
                assertEquals(PairingFailure.Reason.RESPONSE, failure.reason)
            }
            healthBody.set("{\"status\":\"ok\",\"protocolVersion\":1}")
            rejectHealth.set(true)
            assertThrows(PairingFailure::class.java) { runBlocking { client.checkStatus() } }
            val afterRejection = requests.get()
            val forgotten = assertThrows(PairingFailure::class.java) { runBlocking { client.checkStatus() } }
            assertEquals(PairingFailure.Reason.EXPIRED, forgotten.reason)
            assertEquals(afterRejection, requests.get())
            rejectHealth.set(false)
            for (field in listOf("token", "expiresAt", "deviceId")) {
                val invalid =
                    JSONObject()
                        .put("token", token)
                        .put("expiresAt", System.currentTimeMillis() + 60000)
                        .put("deviceId", UUID.randomUUID().toString())
                invalid.put(field, if (field == "expiresAt") "9999999999999" else 123)
                pairingOverride.set(invalid.toString())
                val failure = assertThrows(PairingFailure::class.java) { runBlocking { client.pair(challenge, "01234567") } }
                assertEquals(PairingFailure.Reason.RESPONSE, failure.reason)
                assertThrows(PairingFailure::class.java) { runBlocking { client.checkStatus() } }
            }
            pairingOverride.set(null)
            runBlocking {
                client.pair(challenge, "01234567")
                delayHealth.set(true)
                val check = async { client.checkStatus() }
                try {
                    assertTrue(withContext(Dispatchers.IO) { healthStarted.await(2, TimeUnit.SECONDS) })
                    client.forget()
                } finally {
                    releaseHealth.countDown()
                }
                try {
                    check.await()
                    fail("Stale health response reported success")
                } catch (_: CancellationException) {
                }
                delayHealth.set(false)
            }
            client.forget()
            assertThrows(PairingFailure::class.java) { runBlocking { client.checkStatus() } }
            val before = requests.get()
            val wrong = PairingClient(HostTrust("127.0.0.1", server.address.port, "00".repeat(32)))
            assertThrows(SSLException::class.java) { runBlocking { wrong.pair(challenge, "01234567") } }
            assertEquals(before, requests.get())
            responseStatus.set(429)
            val limited = assertThrows(PairingFailure::class.java) { runBlocking { client.pair(challenge, "01234567") } }
            assertEquals(PairingFailure.Reason.RATE_LIMITED, limited.reason)
            responseStatus.set(201)
            largeResponse.set(true)
            assertThrows(PairingFailure::class.java) { runBlocking { client.pair(challenge, "01234567") } }
            largeResponse.set(false)
            redirect.set(true)
            val beforeRedirect = requests.get()
            assertThrows(PairingFailure::class.java) { runBlocking { client.pair(challenge, "01234567") } }
            assertEquals(beforeRedirect + 1, requests.get())
            redirect.set(false)
            delayResponse.set(true)
            runBlocking {
                val attempt = async { client.pair(challenge, "01234567") }
                try {
                    assertTrue(withContext(Dispatchers.IO) { requestStarted.await(2, TimeUnit.SECONDS) })
                    client.forget()
                } finally {
                    releaseResponse.countDown()
                }
                try {
                    attempt.await()
                    fail("Cleared connection was restored")
                } catch (_: CancellationException) {
                }
                try {
                    client.checkStatus()
                    fail("Credential survived disconnect")
                } catch (_: PairingFailure) {
                }
            }
            server.stop(0)
            command(
                "openssl",
                "req",
                "-new",
                "-x509",
                "-key",
                key,
                "-days",
                "1",
                "-subj",
                "/CN=Wrong Host",
                "-addext",
                "subjectAltName=IP:10.0.0.1",
                "-out",
                cert,
            )
            command(
                "openssl",
                "pkcs12",
                "-export",
                "-in",
                cert,
                "-inkey",
                key,
                "-out",
                bundle.toString(),
                "-passout",
                "stdin",
                input = "fixture-only\n",
            )
            val wrongKeys =
                KeyStore
                    .getInstance(
                        "PKCS12",
                    ).apply { Files.newInputStream(bundle).use { load(it, "fixture-only".toCharArray()) } }
            val wrongManager =
                KeyManagerFactory.getInstance(KeyManagerFactory.getDefaultAlgorithm()).apply {
                    init(wrongKeys, "fixture-only".toCharArray())
                }
            val wrongContext = SSLContext.getInstance("TLS").apply { init(wrongManager.keyManagers, null, null) }
            val wrongLeaf = wrongKeys.getCertificate(wrongKeys.aliases().nextElement()) as X509Certificate
            val correctPinWrongHost = MessageDigest.getInstance("SHA-256").digest(wrongLeaf.encoded).joinToString("") { "%02x".format(it) }
            server =
                HttpsServer.create(InetSocketAddress("127.0.0.1", 0), 0).apply {
                    httpsConfigurator = HttpsConfigurator(wrongContext)
                    createContext("/") { exchange ->
                        requests.incrementAndGet()
                        exchange.close()
                    }
                    start()
                }
            val wrongHostClient = PairingClient(HostTrust("127.0.0.1", server.address.port, correctPinWrongHost))
            val beforeWrongHost = requests.get()
            assertThrows(SSLException::class.java) { runBlocking { wrongHostClient.pair(challenge, "01234567") } }
            assertEquals(beforeWrongHost, requests.get())
        } finally {
            server?.stop(0)
            Files.walk(directory).use { paths -> paths.sorted(Comparator.reverseOrder()).forEach { Files.delete(it) } }
        }
    }
}

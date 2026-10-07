# Android Pairing Validation

The Android app supports manual HTTPS pairing and authenticated health checks.
It does not stream games, send input, or open a WebRTC connection.

## Setup

1. Build the APK using the commands in [Building](../BUILDING.md#android).
   JVM networking tests also require OpenSSL on the build machine.
2. Provision and inspect a host identity using the
   [TLS setup guide](signaling-tls.md). Use the PC's actual private LAN IPv4
   address, not 127.0.0.1 (which means the phone itself).
3. Start signaling with that TLS identity, explicit private LAN bind, LAN opt-in,
   and the existing administrator token. Keep the phone on the same trusted LAN.
   This guide does not open firewall ports or configure public access.
4. Install `android/app/build/outputs/apk/debug/app-debug.apk` on the phone.
   With an explicitly authorized USB-debugging connection, use
   `adb install -r android/app/build/outputs/apk/debug/app-debug.apk` from the repo root.
5. Enter the PC IP, signaling port (default 8787), and SHA-256 certificate
   fingerprint printed on the trusted PC. Compare all bytes and select
   "Fingerprint confirmed on PC". Editing the endpoint or fingerprint clears
   that confirmation. No certificate is trusted automatically.
6. On the PC, use the administrator-authenticated `POST /v1/pairing/start`
   endpoint with `{}` to obtain a challenge ID and code. See the
   [pairing API](../protocols/signaling.md#local-ephemeral-pairing).
   Do not enter the administrator token on the phone or share it in chat/logs.
7. Enter the challenge UUID and eight-digit code on the phone before its
   two-minute expiry. Pairing succeeds only after an authenticated health check.
   Use Check status to repeat the check, or Disconnect to forget the credential.

## Trust and Lifecycle

The client pins SHA-256 of the full leaf DER certificate, checks validity and
certificate trust, and retains the platform IP/subjectAltName verifier. It does
not replace global TLS settings. Redirects are rejected. Response bodies are
limited to 4096 bytes, with connection/read timeouts and cancellation checks.
Codes and tokens are never logged or included in UI status.

Credentials are memory-only, expire after the server's 15-minute lifetime, and
are lost on activity recreation, app restart, failure, or disconnect. Disconnect
is local forgetting, not server revocation: the administrator can revoke the
device through the API. Repeated pairing without revocation can consume the
server's 16 active-device slots until expiry. No Keystore persistence, refresh,
QR onboarding, automatic reconnect, or certificate rotation is implemented.

## Automated and Manual Checks

Networking JVM tests use real loopback TLS sockets and temporary test identities.
They cover successful pairing/health, wrong pins and IP/SAN mismatch before HTTP
delivery, invalid endpoints, rate limiting, oversized responses, redirects, and
forgetting during an in-flight pairing request. Fixtures are removed afterward.
Additional regressions reject coerced JSON field types, invalidate credentials
after an authorization rejection, and prevent an in-flight health response from
reporting success after disconnect. Expiry is checked again after health returns.
These tests do not establish compatibility with a physical Android TLS provider.

On a phone, verify form layout with the keyboard visible, correct and incorrect
fingerprints, expired/wrong codes, status refresh, disconnect, and cancellation.
Rotate the activity during a request and confirm no connection is restored.
Verify that no administrator credentials are requested and no secrets appear in
logs. Physical phone-to-host and UI checks remain unverified; no media acceptance
gate has been passed by these pairing tests.

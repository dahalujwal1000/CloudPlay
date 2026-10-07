# Signaling Protocol

Signaling is not media transport.

## Message envelope

```json
{
  "version": 1,
  "type": "session.offer",
  "requestId": "uuid",
  "sessionId": "uuid",
  "payload": {}
}
```

## Initial messages
- auth.request
- auth.response
- pairing.start
- pairing.complete
- session.create
- session.offer
- session.answer
- ice.candidate
- session.state
- session.close
- error

Every message must be schema validated.

Never place passwords/private keys/game credentials in signaling messages.

## Foundation Diagnostics

The implemented bootstrap server accepts only a diagnostics message on authenticated
WebSocket `/v1/signaling`. The initial session/pairing message list above remains
planned and is rejected until payload schemas and authorization are implemented.

Request (no session exists yet):

```json
{
  "version": 1,
  "type": "service.status",
  "requestId": "cd3aab88-e5ed-4d9e-9e18-ae8f7098da81",
  "payload": {}
}
```

Response echoes `version`, `type`, and `requestId`; payload is
`{"status":"ready","capabilities":[]}`. Ready means the diagnostic service is
available, not that a host or stream is ready.

Messages reject unknown fields, unsupported versions/types, non-UUID request IDs,
binary frames, and nonempty request payloads. Invalid messages close with code 1008;
frames over 4096 bytes close with 1009. Each connection accepts at most 60 messages,
expires after 60 seconds, and checks outbound buffering against a 16 KiB threshold.
HTTP requests/upgrades are rate-limited to 60 per minute per source IP. All endpoints
require a bearer token, except the pairing completion endpoint which authenticates
using its one-use challenge and code. See `docs/BUILDING.md`.

## Local Ephemeral Pairing

This is an ephemeral diagnostics foundation, not production host/device identity.
HTTP is loopback-only; explicitly enabled private LAN access requires
[Secret Service-backed TLS](../testing/signaling-tls.md).
The administrator bearer token comes from CLOUDPLAY_TOKEN and must never be sent
to the phone. A future trusted local host UI will present the challenge and code.
No pairing WebSocket messages or media/session-control permissions are added.

| Route | Authentication | Body | Success |
| --- | --- | --- | --- |
| POST /v1/pairing/start | Administrator bearer | `{}` | 201: challengeId, code, expiresAt |
| POST /v1/pairing/complete | One-use challenge and code | challengeId (UUID), code (8 ASCII digits) | 201: deviceId, token, expiresAt |
| POST /v1/pairing/revoke | Administrator bearer | deviceId (UUID) | 204, including unknown IDs |

Bodies reject extra fields. expiresAt is Unix epoch milliseconds. Responses have
Cache-Control: no-store. Invalid bodies return 400, rejected credentials 401,
rate limits 429, and a full device registry 409 on start. Errors do not echo secrets.

Only one challenge exists at a time: starting another replaces it. Challenges
expire after two minutes and are invalidated after five wrong guesses. Redemption
is synchronous, so concurrent requests cannot consume the same code twice.
There is an additional process-wide ten-attempt/minute budget, including unknown
challenge IDs, which rotation does not reset. HTTP limits are five starts/minute
and ten completions/minute per source IP. No forwarded-IP trust is enabled.

Issued credentials are random 256-bit bearer tokens, valid for 15 minutes, capped
at 16 active devices. Only token digests and keyed code digests are retained.
Device credentials authorize GET /health and the existing diagnostic WebSocket
only; they cannot start/revoke pairing or control a host. Revocation terminates
that device's existing sockets. Each device message rechecks credential validity;
the existing 60-second connection lifetime also bounds idle sockets.

Restart/shutdown removes all challenges and device credentials. There is no
persistent device credential storage or refresh flow yet. Do not
expose this service through a public bind or reverse proxy. TLS identity loading
from Secret Service is implemented. Android supports manual fingerprint-confirmed
HTTPS enrollment and health checks with memory-only credentials; see
[Android pairing validation](../testing/android-pairing.md). Persistent device
storage and host-scoped authorization remain future work. Pairing does not
authorize game control or streaming.

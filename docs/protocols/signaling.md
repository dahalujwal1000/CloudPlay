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
require an environment-provided bearer token. See `docs/BUILDING.md`.

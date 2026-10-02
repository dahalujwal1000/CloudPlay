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

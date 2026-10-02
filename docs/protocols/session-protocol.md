# Session Protocol

Session states:
- OFFLINE
- STARTING
- READY
- PAIRING
- CONNECTING
- CONNECTED
- STARTING_GAME
- STREAMING
- STOPPING

Failures:
- AUTH_FAILED
- NETWORK_FAILED
- GAME_START_FAILED
- CAPTURE_FAILED
- ENCODER_FAILED
- WEBRTC_FAILED
- INPUT_FAILED

Every transition should have a clear owner and observable event.

## Foundation Transition Ownership

The host orchestration thread and Android session owner serialize state mutation.
Core reducers validate transitions; platform owners execute actual side effects.
The current host executable exercises startup/shutdown only.

Normal path: `OFFLINE -> STARTING -> READY`, optional `PAIRING -> READY`, then
`CONNECTING -> CONNECTED -> STARTING_GAME -> STREAMING`.
`GAME_STOPPED` returns `STARTING_GAME`/`STREAMING` to `CONNECTED`.

`STOP` or `FAIL` from an active state enters `STOPPING`. `FAIL` requires a typed
failure code; other events reject failure codes. Disconnect while connecting,
connected, or running a game enters `STOPPING`; canceling pairing returns to `READY`.
Rejected events leave state unchanged.

Only `STOPPED` moves `STOPPING -> OFFLINE`. Before emitting it, the future platform
owner must release held input, stop capture/encoder/media callbacks, close transports,
and await owned work. This cleanup is a contract, not an implemented resource pipeline.
Restart begins with `START` from `OFFLINE`.

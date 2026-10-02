# CloudPlay — AGENTS.md

## Mission
CloudPlay is a personal cloud-gaming platform. Windows performs game rendering, capture, hardware encoding, session management, and input handling. Android receives low-latency audio/video and sends user input.

The first supported game is Genshin Impact, but architecture MUST remain game-agnostic.

## Goals
1. Low latency.
2. Stable 1080p60.
3. Reliable reconnect/recovery.
4. Secure device pairing.
5. Clean subsystem boundaries.
6. Professional diagnostics/testing.
7. No game modification.

## Non-goals
Do not implement game memory modification, DLL injection into games, anti-cheat bypass, DRM bypass, credential theft, gameplay automation, botting, unauthorized access, or redistribution of proprietary game files.

Treat games as ordinary user-installed applications.

## Architecture boundaries
- Host.App: orchestration/lifecycle
- Host.Core: domain models/state machines/interfaces
- Host.Capture: Windows Graphics Capture
- Host.Encoder: NVIDIA hardware encoding
- Host.Streaming: WebRTC media/data transport
- Host.Input: keyboard/mouse/controller/input mapping
- Host.Games: game profiles/process lifecycle
- Host.Network: signaling/connectivity
- Host.Security: pairing/authentication/credential handling
- Host.Telemetry: metrics/logs/diagnostics

Android must use equivalent separation.

## Technology
Windows: C++20, CMake, Windows SDK, D3D11, Windows Graphics Capture, WebRTC native, NVIDIA Video Codec SDK.
Android: Kotlin, Jetpack Compose, WebRTC Android, MediaCodec, Coroutines, StateFlow.
Signaling: TypeScript, Node.js, Fastify, WebSocket, schema validation.

## Streaming
Use WebRTC. Do not invent a custom media protocol unless an ADR explicitly approves it.
Initial target: H.264, 1920x1080, 60 FPS, 6–20 Mbps adaptive bitrate, low-latency settings.
Prefer GPU-resident texture paths and minimize GPU↔CPU↔RAM copies.

## Input
Input protocol is versioned. Packets contain protocol version, sequence, timestamp, event type, and validated payload.
Fast-changing input uses latest-state semantics where appropriate. Never allow unbounded input queues.

## Security
Never log passwords, tokens, private keys, pairing secrets, or game credentials.
All control APIs require authentication.
Pairing codes expire and are rate-limited.
Persistent credentials use secure OS storage.
Never expose an unauthenticated host control API to the public Internet.

## Game integration
Allowed: launch process, monitor process, capture output, stream output, receive user input, terminate on user request.
Forbidden: memory editing, DLL injection, anti-cheat bypass, executable patching, undocumented internal game protocol manipulation.

## State management
Use explicit state machines. Avoid unrelated boolean flags representing lifecycle.

## Observability
Track capture latency, encode latency, RTT, jitter, packet loss, decode latency, rendered FPS, dropped frames, bitrate, and session lifecycle.

## AI coding rules
Do not blindly trust generated code.
For significant changes:
1. inspect ownership/lifetime
2. identify concurrency assumptions
3. identify failure modes
4. review security
5. write/run tests
6. run formatting/static analysis
7. update documentation if architecture changed

Never invent external APIs. Verify APIs against official documentation.

## Definition of Done
- implementation complete
- appropriate tests added
- tests/build pass
- formatting/static analysis pass
- errors handled
- docs updated when needed
- unrelated files untouched

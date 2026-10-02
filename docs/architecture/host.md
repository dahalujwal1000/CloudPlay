# Host Architecture

Linux/Fedora is the primary target under ADR-006. Windows remains a secondary
backend; its implemented capture path is not available on Linux.

## Modules

### Host.App
Owns application lifecycle and orchestration.

### Host.Core
Pure domain models, interfaces, state machines, and configuration.

### Host.Capture
Platform-specific acquisition. Linux capture and GPU interoperability are pending
design validation; Windows uses Windows Graphics Capture and D3D11.

### Host.Encoder
NVIDIA hardware encoding. Initial codec: H.264.

### Host.Streaming
WebRTC peer connection, audio/video tracks, DataChannels, ICE integration.

### Host.Input
Keyboard, mouse, gamepad, and mapped touch input.

### Host.Games
GameProfile abstraction, process launch/monitoring, clean shutdown.

### Host.Security
Pairing, device identity, authentication, credential storage.

### Host.Telemetry
Structured logs and session metrics.

## Pipeline

Game
→ platform capture backend
→ GPU frame buffer
→ hardware encoder
→ WebRTC RTP
→ network

Avoid unnecessary CPU copies.

Linux SDK/driver readiness is documented in `docs/testing/linux-nvenc.md`.
No production encoder implementation exists yet.

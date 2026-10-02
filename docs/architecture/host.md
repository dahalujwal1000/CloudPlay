# Host Architecture

Linux/Fedora is the primary target under ADR-006. Windows remains a secondary
backend; its implemented capture path is not available on Linux.

## Modules

### Host.App
Owns application lifecycle and orchestration.

### Host.Core
Pure domain models, interfaces, state machines, and configuration.

### Host.Capture
Linux uses the ScreenCast portal and PipeWire DMA-BUFs behind IFrameCapture,
with NVIDIA EGL import validation. Sustained 1080p60 and NVENC interoperability
are pending. Windows uses the preserved Windows Graphics Capture/D3D11 backend.

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

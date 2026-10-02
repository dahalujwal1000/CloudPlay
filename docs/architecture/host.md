# Windows Host Architecture

## Modules

### Host.App
Owns application lifecycle and orchestration.

### Host.Core
Pure domain models, interfaces, state machines, and configuration.

### Host.Capture
Windows Graphics Capture and D3D11 frame acquisition.

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
→ Graphics Capture
→ D3D11 texture
→ hardware encoder
→ WebRTC RTP
→ network

Avoid unnecessary CPU copies.

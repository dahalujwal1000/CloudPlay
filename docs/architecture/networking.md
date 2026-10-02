# Networking Architecture

## Control plane
Android ↔ HTTPS/WSS ↔ signaling server ↔ Windows host

Handles:
- authentication
- device pairing
- session creation
- SDP exchange
- ICE candidate exchange
- session state

## Media/data plane
Android ↔ WebRTC ↔ Windows host

Carries:
- video
- audio
- input
- telemetry

## Connectivity
Development order:
1. same machine test
2. LAN
3. remote LAN
4. Internet direct ICE
5. TURN fallback

Do not expose an unauthenticated host control API.

# CloudPlay Professional Blueprint

Personal cloud-gaming platform blueprint.

## Product
Windows PC renders a user-installed game. Android acts as a low-latency streaming client with touch/controller input.

## V1 target
- Windows 11
- Android
- NVIDIA RTX 3050 6 GB class hardware
- H.264
- 1080p60
- WebRTC
- LAN first, Internet second
- Genshin Impact as first game profile

## Architecture
Windows:
Game → Graphics Capture → D3D11 texture → NVENC → WebRTC

Android:
WebRTC → hardware decoder → Surface

Input:
Touch/controller → input abstraction → WebRTC DataChannel → validated host input adapter

## Build order
1. Foundation/CI
2. Windows capture
3. NVENC
4. PC-to-PC WebRTC
5. Android video
6. Input
7. Game manager
8. Pairing/security
9. Internet/TURN
10. Adaptive bitrate/recovery
11. Release hardening

See docs/ for detailed specifications.

## Foundation implementation
The native core, Android client scaffold, and authenticated signaling diagnostics
are implemented. Capture and streaming remain upcoming tasks.

See [build instructions](docs/BUILDING.md), the
[agent handoff](docs/AGENT_HANDOFF.md), and
[foundation acceptance criteria](docs/tasks/TASK-001-foundation.md).

## Important
This repository is a development blueprint, not a claim that proprietary game files or services are included.

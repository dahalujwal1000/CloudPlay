# CloudPlay Professional Blueprint

Personal cloud-gaming platform blueprint.

## Product
Linux PC (Fedora first) renders a user-installed game. Android acts as a low-latency streaming client with touch/controller input. Windows remains a secondary backend.

## V1 target
- Fedora Linux
- Android
- NVIDIA RTX 3050 6 GB class hardware
- H.264
- 1080p60
- WebRTC
- LAN first, Internet second
- Genshin Impact as first game profile

## Architecture
Linux (planned):
Game → Linux capture backend → GPU buffer → NVENC → WebRTC

Linux capture uses GNOME's ScreenCast portal, PipeWire DMA-BUFs and NVIDIA EGL imports.
Sustained 1080p60 and NVENC interoperability remain pending. Existing Windows
Graphics Capture/D3D11 code is preserved; see [Linux host decision](docs/decisions/ADR-006-linux-host.md).

Android:
WebRTC → hardware decoder → Surface

Input:
Touch/controller → input abstraction → WebRTC DataChannel → validated host input adapter

## Build order
1. Foundation/CI
2. Linux capture and GPU interoperability (Windows capture retained)
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
are verified in CI. A Windows capture module and diagnostic are implemented;
live Windows hardware acceptance is unverified. Development now targets Linux:
NVIDIA SDK interface headers and driver readiness diagnostics are set up, with
H.264/HEVC synthetic hardware tests passing. Linux GPU capture now has a standalone
diagnostic behind IFrameCapture and lifecycle tests; sustained 1080p60 acceptance
and media streaming remain pending. See [Linux NVIDIA setup](docs/testing/linux-nvenc.md)
and [capture checks](docs/testing/linux-capture.md).

See [build instructions](docs/BUILDING.md), the
[agent handoff](docs/AGENT_HANDOFF.md), and
[foundation acceptance criteria](docs/tasks/TASK-001-foundation.md).

## Important
This repository is a development blueprint, not a claim that proprietary game files or services are included.

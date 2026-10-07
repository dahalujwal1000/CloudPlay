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

## Foundation implementation and current validation status
The native core, Android pairing client, authenticated signaling diagnostics,
Linux capture diagnostics, input validation, game profile validation, and NVIDIA
SDK/interface readiness checks are all passing in the repo's configured CMake
build/test stages. This includes the Linux capture lifecycle, portal shutdown,
frame-policy, frame-lease, NVENC driver checks, and host/input/game session
validation suites.

This is a validated stage-completion status, not a claim that the end-to-end
cloud-gaming product is complete. Live sustained 1080p60 acceptance on a real
desktop, captured-frame NVENC interoperability, WebRTC media transport, Android
video, production credential persistence/recovery, and remote internet connectivity remain pending.

As of 2026-10-07, Android has a certificate-pinned HTTPS pairing form and
authenticated status checks. The user verified PC TLS identity provisioning and
HTTPS startup; phone pairing returned an authorization rejection and successful
enrollment is not yet verified. This UI cannot stream games. See
[phone pairing setup and troubleshooting](docs/testing/android-pairing.md).
Latest capture controls measured 38.20 FPS at the default ceiling and 40.80 FPS
with an experimental ceiling, both below acceptance. No captured-frame
NVENC/WebRTC integration has been enabled.
See [Linux NVIDIA setup](docs/testing/linux-nvenc.md), [capture checks](docs/testing/linux-capture.md),
and the roadmap in [docs/TASKS.md](docs/TASKS.md).

See [build instructions](docs/BUILDING.md), the
[agent handoff](docs/AGENT_HANDOFF.md), and
[foundation acceptance criteria](docs/tasks/TASK-001-foundation.md).

## Important
This repository is a development blueprint, not a claim that proprietary game files or services are included.

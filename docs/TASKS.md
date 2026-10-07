# Initial Task Roadmap

## TASK-001 Foundation
Create repository, CMake, Android project, CI, formatting, linting, test frameworks, config, logging.

Status: foundation scaffolding verified locally and in Linux/Windows/Android/signaling CI.
The 2026-10-05 CI update enables all implemented Linux backends and builds with
clang-tidy using target compiler arguments. Its clean local build, analysis and
46 native tests pass; execution of the updated hosted workflow remains pending.
See [acceptance criteria](tasks/TASK-001-foundation.md) and [agent handoff](AGENT_HANDOFF.md).

## TASK-002 Host Capture
Primary target is now Linux under ADR-006. Choose and implement consent-based
Linux capture and GPU interoperability. Preserve Windows Graphics Capture → D3D11 texture.

Status: the Linux capture implementation and validation suites pass in the repo's
configured CMake builds, including lifecycle, frame-policy, timing, portal shutdown,
and acceptance-logic checks. This is stage completion evidence for the implemented
capture stack, not proof of a real 1080p60 live hardware acceptance gate.

Live Windows GPU acceptance is pending and is no longer the primary development gate.
Linux portal/PipeWire capture behind IFrameCapture and a GPU-import diagnostic are
implemented. Live 1920x1080 DMA-BUF/NVIDIA imports work, but the measured cadence
in real desktop runs still does not satisfy the sustained 1080p60 gate. See
[Linux checks](testing/linux-capture.md) and [existing Windows capture task](tasks/TASK-002-capture.md).
Owner-thread capture lifecycle/restart orchestration is now implemented as a
standalone Host.App library with injected-backend tests. No automatic consent
retries are performed. Host executable wiring is implemented as an opt-in Linux
capture diagnostic; live recovery verification remains pending.
Live host consent/start/timed shutdown passed with all GPU buffers returned;
sharing revocation and live restart remain pending.
See [capture session checks](testing/capture-session.md). Priority FPS investigation
resumed on 2026-10-05: a native Wayland source verifies presentation near 60 FPS,
but GPU monitor capture still averages 36.77 FPS and the CPU control stalled.
Next isolate monitor/window source-node delivery; encoder and WebRTC integration
gates remain unchanged. See [presentation controls](testing/wayland-motion-source.md).

## TASK-003 NVENC
Implement H.264 hardware encoding and benchmark.

Status: the NVENC interface readiness and driver-validation checks pass in the
repo's configured build/test suites. This validates the platform integration
path and hardware API readiness checks, but it does not prove a production
encoder path or captured-frame interoperability. AV1 hardware encode is
unsupported on the tested RTX 3050. Production encoder code has deliberately not
started. See [Linux NVIDIA checks](testing/linux-nvenc.md).

## TASK-004 PC-to-PC WebRTC
Stream video/audio to a desktop test client.

Gated: do not start integration before stable 1080p60 GPU capture and captured-frame
NVENC interoperability are verified. The capture acceptance gate has not passed.

## TASK-005 Android Video
Receive and render WebRTC video on Android.

## TASK-006 Input
Implement DataChannel input protocol and host validation.

Status: independent Host.Input v1 keyboard/mouse wire decoder and session-bound
replay validation plus bounded FIFO dispatch and held-key/button cleanup are
implemented and tested with fake sinks. Overflow and sink failure close admission;
failed releases require explicit cleanup retry. An optional Fedora/Wayland
RemoteDesktop portal sink and standalone consent/input diagnostic are implemented
with private-D-Bus tests. Live GNOME consent/start/timed shutdown passed without
emitting events; actual event delivery/revocation and production latency are pending.
No transport, authenticated endpoint or Android sender is connected. Host.App owns the sink and
dispatcher via InputSession with tested disconnect/stop/failure/game-stop hooks;
the hook is not yet wired into a live authorized host session.
Touch/controller support remains pending. See [input protocol](protocols/input-protocol.md).
Native adapter checks: [Linux input](testing/linux-input.md).

## TASK-007 Touch Controls
Implement virtual joystick, buttons, camera controls and editable layouts.

## TASK-008 Game Manager
Implement GameProfile and process lifecycle.

Status: pure bounded GameProfile validation and IGameProcess interface implemented;
optional Linux direct-child launch/poll/explicit stop/detach implemented with GIO
and fixture-process tests. No actual game launched. Trusted local key-file catalog
loading and a validation-only diagnostic are implemented and tested with bounded
snapshots, owner/mode checks, symlink rejection and exact-ID lookup.
Host.App GameSession now owns the catalog/backend and launches approved IDs through
a standalone explicit-consent local runner, tested with fixtures. Production peer
authorization/main-host session wiring, launcher/descendant supervision, Windows process
backend and game compatibility remain pending. Destruction does not kill a game;
force stop requires an explicit request. See [Linux game checks](testing/linux-games.md).
Profile schema and trust policy: [local profiles](testing/local-game-profiles.md).
Try the local path: [game runner](testing/game-runner.md).

## TASK-009 Pairing
Implement secure pairing and device identity.

## TASK-010 Internet Connectivity
Implement signaling, STUN, TURN and reconnect.

## TASK-011 Adaptive Streaming
Implement bitrate adaptation and telemetry.

## TASK-012 Release Hardening
Crash handling, installers, diagnostics, compatibility tests, security review and release candidate testing.

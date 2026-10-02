# Initial Task Roadmap

## TASK-001 Foundation
Create repository, CMake, Android project, CI, formatting, linting, test frameworks, config, logging.

Status: foundation scaffolding verified locally and in Linux/Windows/Android/signaling CI.
See [acceptance criteria](tasks/TASK-001-foundation.md) and [agent handoff](AGENT_HANDOFF.md).

## TASK-002 Host Capture
Primary target is now Linux under ADR-006. Choose and implement consent-based
Linux capture and GPU interoperability. Preserve Windows Graphics Capture → D3D11 texture.

Status: capture module/diagnostic build and tests pass in Windows/Linux CI;
live Windows GPU acceptance is pending and is no longer the primary development gate.
Linux portal/PipeWire capture behind IFrameCapture and a GPU-import diagnostic are
implemented. Live 1920x1080 DMA-BUF/NVIDIA imports work; observed 35-42 FPS does not
pass sustained 1080p60. See [Linux checks](testing/linux-capture.md) and
[existing Windows capture task](tasks/TASK-002-capture.md).

## TASK-003 NVENC
Implement H.264 hardware encoding and benchmark.

Status: Linux SDK interface 13.1.15 set up locally; driver API readiness and
synthetic H.264/HEVC hardware checks pass. AV1 hardware encode is unsupported on
the tested RTX 3050. Production encoder code has deliberately not started.
See [Linux NVIDIA checks](testing/linux-nvenc.md).

## TASK-004 PC-to-PC WebRTC
Stream video/audio to a desktop test client.

Gated: do not start integration before stable 1080p60 GPU capture and captured-frame
NVENC interoperability are verified. The capture acceptance gate has not passed.

## TASK-005 Android Video
Receive and render WebRTC video on Android.

## TASK-006 Input
Implement DataChannel input protocol and host validation.

## TASK-007 Touch Controls
Implement virtual joystick, buttons, camera controls and editable layouts.

## TASK-008 Game Manager
Implement GameProfile and process lifecycle.

## TASK-009 Pairing
Implement secure pairing and device identity.

## TASK-010 Internet Connectivity
Implement signaling, STUN, TURN and reconnect.

## TASK-011 Adaptive Streaming
Implement bitrate adaptation and telemetry.

## TASK-012 Release Hardening
Crash handling, installers, diagnostics, compatibility tests, security review and release candidate testing.

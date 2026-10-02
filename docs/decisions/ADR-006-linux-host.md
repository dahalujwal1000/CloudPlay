# ADR-006: Linux-First Host

Status: Accepted by user direction on 2026-10-02.

## Decision

Fedora Linux is the primary host target, replacing Windows as the V1 development
and hardware acceptance platform. Android and WebRTC remain unchanged. The native
host stays C++20/CMake and uses NVIDIA NVENC for the initial H.264 target.

Keep the existing Windows WGC/D3D11 code and Windows CI as a secondary backend;
do not port Windows capture calls into Linux or erase prior verification history.
ADR-003 still describes the Windows backend, not Linux capture.

## Consequences

- Linux capture needs a new backend. Evaluate consent-based desktop portals and
  PipeWire for Wayland; select and verify GPU-buffer interoperability before coding.
- Linux NVENC must use a supported Linux device path (CUDA or OpenGL), not D3D11.
  Decide ownership, synchronization, conversion and bounded buffering before encoding.
- CUDA UMD compatibility reported by nvidia-smi is not an installed CUDA Toolkit.
- Validate the installed driver API and live H.264 capability before encoder code.
- Keep game launch/input behind platform-specific adapters. Linux game availability
  is not established; do not bypass anti-cheat or modify a game to make it run.

## Current Scope

The original decision introduced SDK/readiness setup. ADR-007 now adds Linux
portal/PipeWire DMA-BUF capture and NVIDIA EGL import diagnostics. Sustained
1080p60 acceptance is pending; no production encoder, WebRTC media path or game
compatibility is claimed.

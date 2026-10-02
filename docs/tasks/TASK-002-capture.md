# TASK-002 Windows Capture

## Goal
Acquire a user-selected window as GPU-resident D3D11 textures with bounded frame
handling, clear lifetimes, resize recovery, and useful diagnostics.

## Scope
Host.Capture, capture probe, tests/build integration, CI, and capture documentation.

## Non-goals
NVENC, preview rendering, audio, game launch, input, streaming, HDR/tone mapping.

## Requirements
- Windows Graphics Capture with a high-performance hardware D3D11 adapter.
- Explicit lifecycle and one owner thread; callbacks do not own the capture object.
- Two-buffer free-threaded pool, bounded draining, no CPU readback or frame queue.
- Do not expose a texture after its frame is checked back into the pool.
- Recreate on resize and discard zero-sized/minimized or mismatched frames.
- Report invalid targets, unsupported capture, permission denial, target close,
  device loss, and other platform HRESULT failures.
- Keep capture indicators and require an explicit user-provided HWND.

## Acceptance Criteria
- [x] Portable policy tests cover invalid sizes and resize.
- [x] Module/probe and owner/lifetime documentation added.
- [x] Linux build/tests, format check, and GCC analysis pass.
- [ ] Windows/MSVC build and validation tests pass for the capture revision.
- [ ] Probe delivers textures on the user's Windows 11/RTX 3050 PC.
- [ ] Resize, minimize/restore, target close, and repeated start/stop verified live.
- [ ] Permission/device-loss handling verified on Windows where feasible.

## Tests
Linux checks frame policy. Windows CI additionally checks invalid HWND rejection,
failure/stop states, owner-thread enforcement, and CLI validation. These tests do
not initialize a GPU. Manual acceptance is in `docs/testing/capture.md`.

## Security Considerations
Capture is limited to the specified user-owned window. No titles, pixels, secrets,
or game internals are logged. No capture exclusions/permission bypass or remote
capture controls are implemented.

## Performance Considerations
SDR BGRA8 stays GPU-resident. Each poll drains at most two frames and synchronously
lends one texture. Consumers must finish GPU use or copy to owned GPU storage
before returning. Counters cover local discards, not all internal WGC losses.

## Documentation
Capture architecture, manual checklist, build instructions, and agent handoff.

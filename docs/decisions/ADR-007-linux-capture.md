# ADR-007: Wayland Portal and PipeWire GPU Capture

Status: Accepted for the Linux milestone; sustained performance acceptance pending.

## Decision

Use GNOME's XDG ScreenCast portal for consent and its restricted PipeWire remote
FD for stream access. Implement `PipeWireCapture` behind `IFrameCapture`.
Preserve the Windows WGC/D3D11 backend without modification.

Negotiate packed RGB DMA-BUF formats/modifiers advertised by NVIDIA's EGL device
corresponding to CUDA device 0. Import delivered buffers as EGL images there.
Reject unsupported imports instead of silently mapping pixels or using a CPU
fallback in the initial GPU acceptance path.

## Ownership

One owner thread handles portal, PipeWire, EGL and consumers. No RT callback
thread or unbounded queue exists. Drain at most eight buffers, matching the
negotiated maximum, and keep the latest. Scope leases return dequeued buffers
exactly once, including consumer exceptions. Release EGL images before buffers.

Images, FDs and views are borrowed until the synchronous consumer returns.
Consumers must complete GPU work before returning. Duplicating an FD does not
prevent producer reuse. Future asynchronous encoding needs owned GPU storage,
bounded buffering and tested synchronization, not retained borrowed views.
Only implicit DMA-BUF synchronization is negotiated; explicit-sync timelines are
not advertised and the diagnostic issues no asynchronous GPU pixel reads.
Wait for DMA-BUF POLLIN write-fence completion before import. Reject unexpected
explicit-sync metadata rather than pretending to support its timeline protocol.

## Boundaries

An EGL image is not an NVENC input handle. NVIDIA-device import is verified, but
CUDA registration, NVENC submission and encoding captured frames are not.
The repo has readiness diagnostics and external FFmpeg capability checks, not a
production encoder. Existing NVENC tests remain unchanged.

Normal capture maps/saves no pixels; capture-module copy counts are zero.
An explicit diagnostic-only `--snapshot` maps a linear packed-RGB DMA-BUF with
CPU SYNC START/END READ while leased, saves a private PNG, and optionally compares
an immutable reference exactly. It reports its CPU copy separately and does not
qualify as performance acceptance. Unsupported layout/crop/sync paths fail.
Compositor/driver internal and cross-device copies remain unknown.
WebRTC remains gated on stable 1080p60 GPU capture and encoder interoperability.
NVENC integration also requires pixel-correct diagnostic frames; exact CPU RGB
comparison passed on the controlled static reference, not yet EGL sampling/encoding.

## Sources

- [ScreenCast portal](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.ScreenCast.html)
- [PipeWire DMA-BUF negotiation](https://docs.pipewire.org/devel/page_dma_buf.html)
- [EGL import modifiers](https://registry.khronos.org/EGL/extensions/EXT/EGL_EXT_image_dma_buf_import_modifiers.txt)
- [EGL/CUDA device identity](https://registry.khronos.org/EGL/extensions/NV/EGL_NV_device_cuda.txt)
- [DMA-BUF CPU access and fences](https://docs.kernel.org/driver-api/dma-buf.html)

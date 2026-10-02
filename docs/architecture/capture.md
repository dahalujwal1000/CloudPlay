# Windows Capture Implementation

`CloudPlay::Capture` is a Windows-only C++20 library using the Windows SDK's
C++/WinRT projection. It links D3D11, DXGI, WindowsApp, and User32. The portable
frame policy also builds on Linux. MSVC and a recent Windows SDK are required.

## Ownership and Lifecycle

The owner initializes a WinRT apartment before constructing `WindowCapture`.
Construction, start, poll, stop, and destruction belong to that thread. Methods
reject other threads. `Idle -> Starting -> Capturing -> Delivering -> Capturing`
is the frame path. Stop closes resources and ends at `Stopped`; platform failure
closes resources and records `Failed`. Failed/stopped capture can restart.

Capture owns devices, capture item, frame pool, session, and the item-close
subscription. Shutdown revokes the event, closes session/pool, then releases
devices. Destruction does best-effort cleanup. The close callback holds only a
weak atomic notification. It cannot invoke an owner or close platform objects.

## Acquisition and Frame Lifetime

DXGI selects a high-performance hardware adapter, skipping software adapters.
There is no WARP fallback. The D3D11 device enables BGRA interop. `CreateForWindow`
receives the explicit HWND; a two-buffer `CreateFreeThreaded` pool captures BGRA8
SDR with the system's capture indicators.

The owner polls `TryGetNextFrame` without a FrameArrived handler. Each poll drains
at most two frames, favoring the latest. Its synchronous consumer receives a
borrowed `ID3D11Texture2D*`, content size, QPC-derived timestamp in 100 ns units,
and estimated capture-to-acquisition latency in microseconds.

**The texture is valid only during that consumer call.** Do not retain it, submit
GPU work that outlives the call, reenter capture, or destroy capture from the
consumer. A future encoder must finish using the texture or copy into a bounded
encoder-owned GPU pool before returning. There is no CPU readback. RAII closes
each frame, including when a consumer throws.

## Recovery and Limits

Zero/negative dimensions are discarded. Resize closes the old frame before pool
recreation and delivers a subsequent frame at the new size, avoiding clipped
content and undefined edges. Window-close and device-removal checks precede
acquisition. Permission/device-loss HRESULTs are classified and propagated; the
caller decides whether to restart. No automatic retry or unbounded queue exists.

Diagnostics report received, delivered, discarded, resize count, and last latency.
They do not count every WGC-internal loss or prove 60 FPS. HDR can look incorrect
in this SDR path; HDR/tone mapping is deferred. Audio is not captured.

## API References
- [Window capture interop](https://learn.microsoft.com/en-us/windows/win32/api/windows.graphics.capture.interop/nf-windows-graphics-capture-interop-igraphicscaptureiteminterop-createforwindow)
- [Free-threaded pool](https://learn.microsoft.com/en-us/uwp/api/windows.graphics.capture.direct3d11captureframepool.createfreethreaded)
- [Frame lifetime, resize, and QPC guidance](https://learn.microsoft.com/en-us/windows/apps/develop/media-authoring-processing/screen-capture)
- [D3D11 device interop](https://learn.microsoft.com/en-us/windows/win32/api/windows.graphics.directx.direct3d11.interop/nf-windows-graphics-directx-direct3d11-interop-createdirect3d11devicefromdxgidevice)

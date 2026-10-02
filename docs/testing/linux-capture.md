# Fedora GNOME/Wayland Capture

## Build

The Linux backend is opt-in, preserving SDK-free domain builds and Windows capture.
The user installed the Fedora development prerequisites:

```sh
sudo dnf install pipewire-devel glib2-devel mesa-libEGL-devel libdrm-devel
# Optional pixel snapshot/reference comparison:
sudo dnf install libpng-devel
cmake -S . -B build/linux-capture -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCLOUDPLAY_LINUX_CAPTURE=ON
cmake --build build/linux-capture
ctest --test-dir build/linux-capture --output-on-failure
build/linux-capture/host/capture/cloudplay_linux_capture_probe --seconds 30
```

Use the normal logged-in GNOME Wayland user, not sudo, SSH, a container or another
user's bus. Session D-Bus and GPU device access are required. Sandbox IPC/device
restrictions are distinct from driver failures. Linux capture plus NVENC readiness
builds run 16 CTest cases. No encoder, CUDA Toolkit or WebRTC is needed here.

## Consent and Gate

Select one 1920x1080 monitor/window in GNOME's sharing dialog. Start/consent timeout
is 120 seconds; other portal responses 30 seconds, D-Bus calls five seconds,
first-frame negotiation ten seconds and close calls one second. No persistent
permission, restore token or remote input is requested.

Keep continuous motion visible for 30 seconds. Static/damage-driven sources may
produce fewer frames than display refresh. The diagnostic requests max 60 FPS;
it does not duplicate frames, scale a wrongly sized source or alter display settings.
Use a 1920x1080/60 Hz display mode for reproducible initial acceptance.

Optional synthetic motion, using installed FFplay:

```sh
SDL_VIDEODRIVER=wayland ffplay -f lavfi -i testsrc2=size=1920x1080:rate=60 -an -fs -autoexit -t 90 -window_title 'CloudPlay Capture Motion Test'
```

Run the probe in another terminal and select that monitor. Escape closes the pattern.
Do not close the selected window or stop sharing before the probe finishes.
Normal FPS probes save no screen/game content and never stream it.

Gate: >=30 seconds, >=29 one-second intervals, average >=59 FPS, every interval
>=58 FPS, 1920x1080, all frames imported on NVIDIA, no local discards/sequence gaps,
no CPU frames/copies and available presentation timestamps. Exit 0: gate passed;
1: runtime failure; 2: bad CLI; 3: finished but not stable. Short smoke tests return
3; `--help` exits 0 without capture. Capture success alone is not NVENC acceptance.

## Metrics

JSON records include FPS, resolution, DRM FourCC/modifier, received/delivered/discarded
counts, sequence gaps, GPU imports, CPU frames, capture-module copy counts and
presentation age, mean/max producer presentation interval, negotiated/max FPS
and mean/max EGL import time. Presentation intervals exclude missing, implausible,
duplicate, backwards and discontinuous timestamps; they describe observed producer
cadence, not the source application's render FPS. FourCC
875713112 is XRGB8888 (BGRx bytes on this little-endian host).
Sequence gaps can include local discards; do not sum them as disjoint losses.
Drops before producer sequence assignment are not observable here.

SPA header PTS is compared with CLOCK_MONOTONIC at delivery. PTS is presentation
time, not guaranteed capture origin. Future PTS values are counted; signed ages
are reported rather than clamped to fake zero latency. Nonnegative age has a
separate mean/max/count. Missing/implausible timestamps are excluded; absent
means are null. Capture-origin latency is explicitly unverified. Internal driver,
compositor and cross-GPU copies are unknown. CPU readback runs only when explicitly
requested with `--snapshot`, separately from performance acceptance.

## Pixel Snapshot

Install libpng development headers and reconfigure/rebuild. Snapshot mode acquires
one frame, copies RGB bytes while the producer buffer is still leased, then stops
capture before PNG compression. Files are created with mode 0600 and existing
files are never overwritten. Captures can contain private screen content: select
only the synthetic pattern for these tests. Paths and raw pixels are not logged.

```sh
ffmpeg -f lavfi -i testsrc2=size=1920x1080:rate=60 -frames:v 1 -update 1 /tmp/cloudplay-reference.png
SDL_VIDEODRIVER=wayland ffplay -loop 0 -i /tmp/cloudplay-reference.png -vf format=bgra -an -fs
build/capture-and-nvenc/host/capture/cloudplay_linux_capture_probe --snapshot /tmp/cloudplay-new-capture.png --reference /tmp/cloudplay-reference.png
```

Select the reference monitor in the portal. Compare an immutable static reference,
not an animated frame of unknown timestamp. FFmpeg `testsrc2` intentionally contains
color bars, diagonal colored lines, and checkerboard regions: these shapes alone
are not evidence of capture corruption.

`capture.format` and `capture.buffer` report the exact SPA format/name, DRM FourCC,
modifier, memory types/DMA-BUF status, plane count, map/chunk/effective offsets,
strides, chunk/allocation sizes, data flags, crop and transform presence/values,
and fence readiness. GPU frames are DMA-BUF storage with borrowed NVIDIA EGLImage
views; the snapshot maps the DMA-BUF, not the EGLImage. EGL import alone is not
pixel correctness. A failure emits the last buffer layout and a specific operation.

Only one-plane, linear, packed 8-bit RGB/BGR is read on little-endian hosts.
RGB bytes are reordered only as required by the negotiated FourCC; X/alpha is
discarded for RGB comparison. No scaling, YUV conversion, rotation, crop repair,
de-tiling guesses or color correction is performed. Row padding is skipped and
the final row is bounds-checked against the actual FD allocation size.
Nonidentity crop/transform, tiled/multiplane buffers, unavailable CPU mapping,
and unsupported formats fail rather than producing a guessed image.

The capture backend waits up to one second per DMA-BUF for POLLIN implicit write
fence completion before EGL import. Explicit-sync timelines are not negotiated;
if unexpectedly present they are rejected, not ignored. CPU reads are additionally
bracketed by DMA_BUF_IOCTL_SYNC START/END READ, with bounded EINTR/EAGAIN retries.
The lease prevents producer reuse until reading and sync cleanup complete.
These follow [kernel DMA-BUF synchronization requirements](https://docs.kernel.org/driver-api/dma-buf.html)
and [PipeWire sync negotiation](https://docs.pipewire.org/devel/page_dma_buf.html).

Snapshot JSON reports one explicit CPU readback/repacking copy, mismatch count,
maximum channel difference, resolution equality and `pixelCorrectVerified`.
Exit 0 means saved (and exact RGB equality when `--reference` is supplied), not
sustained capture acceptance; exit 3 means reference mismatch. No reference means
pixel correctness remains unverified. PNG uses [libpng's simplified API](https://www.libpng.org/pub/png/libpng-manual.txt).

Failures report a typed reason, operation, native numeric error where available
and last frame metrics. Codes: 0 session unavailable; 1 permission/session closed;
2 timeout; 3 portal; 4 PipeWire; 5 unsupported format/buffer; 6 GPU import;
7 consumer. No handles, tokens, titles, pixels or raw external errors are logged.

## Tests

- Frame lease: exactly-once return, exception cleanup, image-before-buffer release.
- CPU snapshot: padding, nonzero offset, channel ordering, row orientation,
  allocation bounds, unsupported layout/crop/transform, unsynchronized read rejection,
  private PNG round trip and no overwrite; no GPU required.
- Gate: short, slow, CPU, wrong-size, dropped and missing-import streams rejected.
- Backend: owner thread, invalid target, absent session, repeated failure/start/stop.
- Presentation timing: future PTS, cadence, discontinuities, missing/invalid,
  duplicate/backwards timestamps and integer extremes without a GPU.
- Private mock D-Bus portal: cancellation at CreateSession, SelectSources and Start,
  exactly-once session close, direct destruction and repeated stop without GNOME/GPU.
- Existing Windows capture and NVENC readiness tests are unchanged.

The mock-bus test requires local Unix socket permission. Run CTest outside this
workspace's IPC-restricted sandbox. CI needs no desktop/GPU. Live asynchronous
GPU completion, explicit sync, resize/recovery and revocation need further testing.

## Observed Results (2026-10-02)

Fedora 44, PipeWire headers 1.6.9, GIO 2.88.3, RTX 3050 6GB, driver 615.71.09.
GNOME reported 1920x1080 at 144.003 Hz; the agent changed no display settings.

Completed 30-second run: 1050 received/delivered DMA-BUF frames, all imported on
NVIDIA, XRGB8888, linear modifier 0, zero local discards/sequence gaps, ~35 FPS.
The initial probe sampled only 24 nonnegative PTS ages (mean 0.507 ms, max 1.260 ms),
not all-frame capture latency. The updated probe separately reports future PTS.

An animated attempt imported 412 frames at ~39-42 FPS before portal session closure.
Unselected attempts timed out cleanly at Start. These runs **do not pass 1080p60**.
The user switched to 60 Hz (GNOME reported 60.003536 Hz). A full 30-second animated
retest delivered 1163 GPU-backed 1920x1080 XRGB8888 frames with modifier 0,
zero local discards/sequence gaps and zero capture-module CPU/GPU copies.
Average FPS was 38.7663; minimum one-second FPS was 34.9703. EGL import averaged
0.272952 ms (max 1.38479 ms); negotiated frame rate was unspecified (0), max 60.
1162 of 1163 PTS values were in the future; signed mean presentation age was
-6.78433 ms, not measured capture-origin latency. Exit 3 correctly rejected the
run. **The 1080p60 gate remains closed.** Producer cadence diagnostics were added
after this retest and still need a live sample. Source rendering, compositor pacing
and cross-device handling need isolation; low import time alone proves no cause.
No WebRTC integration has started; captured-frame NVENC interop remains unverified.

Controlled static-reference capture subsequently passed exact RGB comparison:
1920x1080, SPA BGRx (8), DRM XRGB8888 (875713112), modifier 0, one SPA_DATA_DmaBuf
plane (memory type 3), flags 1, stride 7680, map/chunk/effective offsets 0,
chunk/allocation size 8294400, crop (0,0,1920,1080), no transform metadata, no
explicit-sync metadata, implicit fences ready. CPU SYNC START/END succeeded.
All 2073600 pixels matched: zero mismatched pixels and max channel difference 0.
Saved private PNG: `/tmp/cloudplay-capture-pixel-check.png`; reference:
`/tmp/cloudplay-testsrc2-reference.png`. These temporary artifacts are not committed.
This validates this CPU-readable buffer path, not EGL texture sampling, asynchronous
GPU reuse or NVENC input. Pixel-correct captured-frame encoding and sustained
1080p60 remain gates; neither NVENC implementation nor WebRTC was advanced.

All 15 combined native tests passed locally with IPC access. Native formatting,
GCC analysis and all five [capture CI jobs](https://github.com/dahalujwal1000/CloudPlay/actions/runs/37005794506)
passed at revision `fb5bbbcbd5ed8dd6ff563ea2cfbbadaf4947e9a6`.
Local ASan/UBSan configuration was attempted but cannot link because Fedora
`libasan` and `libubsan` are not installed; no sanitizer pass is claimed.
All five [final source CI jobs](https://github.com/dahalujwal1000/CloudPlay/actions/runs/37006485726)
passed at `59a6b9705bc68076ca70e02987a1ef28acba7bec`, including the final import/FPS
metrics. All 15 combined and five default tests pass locally.

After adding presentation cadence diagnostics and expanded portal-denial tests,
all 15 combined and five default tests passed again, as did clang-format 18 and
GCC `-fanalyzer`. Linux/Windows build/tests and native quality passed in
[the updated source CI run](https://github.com/dahalujwal1000/CloudPlay/actions/runs/37007469783)
at revision `12794b64688dfeff89aaf4cc9badd2a47f9c79af`.

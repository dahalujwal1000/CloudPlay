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
builds run 19 CTest cases. No encoder, CUDA Toolkit or WebRTC is needed here.

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

### Real Desktop Validation

```sh
build/capture-and-nvenc/host/capture/cloudplay_linux_capture_probe --desktop-validation /tmp/cloudplay-real-desktop.png --seconds 30
```

Select a non-sensitive 1920x1080 desktop and keep sharing active. Scroll text,
drag a window and move the cursor throughout the run. This mode requests embedded
cursor composition only after checking the portal's AvailableCursorModes; missing
support fails explicitly. Ordinary probes retain their previous hidden-cursor mode.
See the [ScreenCast portal cursor contract](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.ScreenCast.html).

One owned RGB snapshot is collected after five seconds and another after the
requested duration. After capture stops, private PNGs are saved at the supplied
path and `<path>.last.png`; existing files are never overwritten. Duration must
be 30..120 seconds. CPU readbacks are counted separately as diagnosticCpuCopies,
and this mode always leaves the zero-copy performance gate unpassed. It does not
automatically certify pixel correctness, gradients or temporal motion from stills.
For uncontaminated performance measurement, run the ordinary `--seconds 30` probe
without snapshots, with continuous desktop motion visible.

The 2026-10-02 live desktop run lasted 30.5203 seconds and delivered 921 GPU-backed
frames (30.1766 FPS; minimum one-second interval 13.9977 FPS). All 921 frames were
imported on NVIDIA; local discards were zero, with 17 producer sequence gaps.
Gaps appeared around the first readback; a causal attribution requires a separate
no-readback run. There were two explicit diagnostic CPU copies, no CPU-storage
frames and no reported capture-module GPU copies. Internal driver copies remain
unknown.

Negotiation: SPA BGRx (8), DRM XRGB8888 (875713112), 1920x1080, linear modifier 0,
one DMA-BUF plane (SPA memory type 3), stride 7680, map/chunk/effective offsets 0,
chunk/allocation size 8294400 bytes. Crop was full-frame; no transform or explicit
sync metadata was present. Implicit fences were ready before EGL import.
Negotiated rate was unspecified (0), with maximum 60 FPS. Mean producer interval
was 32.6431 ms, maximum 516.644 ms. Mean EGL import was 0.250475 ms, maximum
0.556064 ms. Of 921 presentation timestamps, 915 were future-dated; signed mean
presentation age was -6.99432 ms. This is not measured capture-origin latency.

Both saved PNGs show readable desktop text, intact borders/icons and visible
cursors without the reported colored artifacts. Content and cursor positions
changed between snapshots. There is no independent desktop pixel reference;
gradient fidelity and temporal motion correctness remain unverified. These
observations do not establish stable 1080p60 or captured-frame NVENC interoperability.
NVENC integration and WebRTC have not started.

### Multi-Frame Buffer Diagnostics

Use `--capture-diagnostics` to trace every acquire/release and each delivered
frame's SPA format, named packed pixel format, width/height, per-plane FD, memory
type, offset/stride/size, modifier, GPU/mapped-CPU status, sync path, crop/transform
and EGL import status. FD numbers are local diagnostic values, not owned handles;
they are printed only in this explicitly requested mode. Do not reuse those values.

```sh
build/capture-and-nvenc/host/capture/cloudplay_linux_capture_probe --capture-diagnostics --generate-reference /tmp/cloudplay-bars.png
SDL_VIDEODRIVER=wayland ffplay -loop 0 -i /tmp/cloudplay-bars.png -vf format=bgra -an -fs
build/capture-and-nvenc/host/capture/cloudplay_linux_capture_probe --capture-diagnostics --cpu-capture --frames 8 --seconds 30 --snapshot /tmp/cloudplay-cpu.png --reference /tmp/cloudplay-bars.png
```

`--cpu-capture` negotiates packed RGB without a modifier, MemFd/MemPtr storage,
PipeWire MAP_BUFFERS and NO_CONVERT; no EGL device/import is required. The exact
mapped plane's chunk offset and stride are used, with allocation/chunk bounds
checks. CPU-copy ownership is tested independently of the producer's memory.
Without `--cpu-capture`, the same mode traces the original DMA-BUF/EGL path and
makes synchronized CPU copies. Compare both paths with the same static reference.

Defaults: eight frames, 30-second collection limit; both accept 1..120. Each frame
is saved privately: first at `--snapshot`, subsequent frames with `.2.png`, etc.
Absent `--snapshot`, a PID-specific `/tmp/cloudplay-diagnostic-<pid>.png` base is
used. Existing files are never overwritten. An interrupted/incomplete or mismatched
series exits 3. Without `--reference`, correctness is explicitly unverified.
No mode here qualifies as GPU/FPS acceptance or starts NVENC/WebRTC.

The reference is immutable, with eight plain RGB bars, a white border and a gray
horizontal marker. It contains no diagonal features, checkerboards or random pixels.
Animated `testsrc2` cannot be compared against a fixed reference at an unknown
timestamp: intentional details change over time.

Packed BGRx/BGRA/RGBx/RGBA interpretation is tested, including padding, nonzero
offset and row orientation. NV12 and CPU multi-plane layouts are deliberately
not negotiated or converted: unsupported formats fail rather than being treated
as RGBA. GPU multi-plane DMA-BUFs remain EGL-import-only; CPU diagnostics reject
them until format/colorimetry-correct plane handling is implemented.
The CPU storage path uses PipeWire's producer/consumer dequeue ownership, not
meaningless DMA-BUF ioctls on a regular MemFd. See [PipeWire stream flags and recycling](https://docs.pipewire.org/group__pw__stream.html).

Each CPU copy completes inside the borrowed callback, before recycling. Its
checksum is recorded while leased and verified after poll returns. PNG writing
and comparison occur only on that owned vector. The summary checks acquired and
returned counts; the existing lease tests verify exactly-once exception cleanup
and image-before-buffer release. Explicit-sync DMA-BUFs remain unsupported and
are rejected rather than ignored.

The user-supplied screenshot `Screenshot From 2026-10-02 18-30-02.png` was inspected
and independently decoded as RGB24. Its frame MD5 is
`2ef828823e00896dc95f16d3f764def0`, identical to the immutable testsrc2 reference
and previous CPU capture. That supplied image contains no added RGB pixels relative
to that reference; this does not rule out unprovided intermittent faulty frames.

First CPU-only series: eight mapped MemFd BGRx frames (memory type 2, flags 9),
1920x1080, stride 7680, zero offsets, 8294400-byte allocations, full-frame crop,
no transform or EGL import. FDs 24/26/27 were reused only after each callback's
owned copy completed. Eight acquisitions and returns matched; all owned-copy
checksums stayed unchanged after release. Every PNG differed from the bars
reference: inspection showed the IDE/sharing dialog instead of the bars.
Exit 3 correctly failed the series. Do not mark CPU-series pixel acceptance as
passed, or attribute that wrong-source fixture to a proven buffer bug.
Saved files: `/tmp/cloudplay-cpu-series.png` and `.2.png` through `.8.png` (private).
Reference: `/tmp/cloudplay-static-bars-reference.png`. A visible-pattern retry is
required; neither NVENC nor WebRTC work has started.

CPU-only retry: eight acquisitions/returns, zero local discards, eight producer
sequence gaps. Frame 2 (`/tmp/cloudplay-cpu-retry.png.2.png`) matched every RGB
pixel of the plain-bars reference exactly. Frame 1 contained the sharing dialog;
frame 3 contained the GNOME panel; later frames showed the IDE. Seven frames
mismatched, so the full series correctly exited 3 and remains unaccepted.
Do not discard these mismatches or claim sustained pixel correctness. A controlled
native window-source test, kept visible throughout, can isolate overlays/focus
changes from the buffer path without cropping/filtering the captured output.
No confirmed corruption was reproduced in the supplied screenshot or exact CPU
frame. Unprovided intermittent failures and GPU sampling still require investigation.

Final diagnostic-mode verification: all 18 combined CTests, five default tests,
clang-format 18, GCC `-fanalyzer`, and all five [CI jobs](https://github.com/dahalujwal1000/CloudPlay/actions/runs/37010793984)
passed at source revision `4ac21355ea607facdc510035d7240e3cb81382dc`.

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
  exactly-once session close, direct destruction and repeated stop without GNOME/GPU;
  embedded-cursor negotiation and unsupported-cursor failure.
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
Independent FFmpeg RGB24 decoding of both PNGs produced the same 6220800-byte
frame MD5 `2ef828823e00896dc95f16d3f764def0`. Final snapshot source passed all
16 combined tests, five default tests, clang-format 18, GCC `-fanalyzer`, and all
five [CI jobs](https://github.com/dahalujwal1000/CloudPlay/actions/runs/37009040112)
at revision `8cd814514964c9596795229e73a5f05b7c227a7e`.

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

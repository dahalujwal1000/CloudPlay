# Wayland Presentation Reference

This optional standalone diagnostic isolates source presentation from PipeWire
capture. It does not encode, inject input, capture the desktop, or start WebRTC.
The Windows backend and working NVENC tests are unchanged.

## Build

Fedora development dependencies: `wayland-devel`, `wayland-protocols-devel`,
`libglvnd-devel`. Protocol bindings are generated with `wayland-scanner` from the
installed official stable `xdg-shell` and `presentation-time` XML, not handwritten.

```sh
cmake -S . -B build/wayland-motion -DCLOUDPLAY_WAYLAND_MOTION=ON
cmake --build build/wayland-motion -j 4
ctest --test-dir build/wayland-motion --output-on-failure
```

The 2026-10-05 workspace check extracted development RPMs under
`/tmp/cloudplay-wayland-dev/usr` without installing system packages. Its local
CMake cache uses that prefix and the installed runtime libraries
`/usr/lib64/libwayland-client.so.0` and `/usr/lib64/libwayland-egl.so.1`.
Reconfigure against installed development packages if this temporary prefix is gone.

## Source-Only Control

Set GNOME Displays to 1920x1080, scale 1, non-VRR 60 Hz. The source is paced by
`wl_surface.frame`, not a timer pretending to be 60 FPS. At higher refresh rates
it follows compositor callbacks; it does not claim a fixed 60 FPS source there.
Fullscreen configuration other than 1920x1080 is refused, not rescaled.

```sh
env -u DRI_PRIME __NV_PRIME_RENDER_OFFLOAD=1 \
  __EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/10_nvidia.json \
  WAYLAND_DEBUG=1 timeout --kill-after=3s 40s \
  build/wayland-motion/host/capture/cloudplay_wayland_motion --seconds 30 \
  > build/wayland-motion/source.jsonl 2> build/wayland-motion/source.log
```

The fullscreen image has six color bars, a moving white stripe and 32 binary
submission-ID tiles along the bottom. Every submission changes content. These
marks are intentional. This is a timing source, not a replacement for previously
validated pixel comparisons; captured frame IDs are not automatically decoded.

`--composition-control` adds a static 48x48 gray EGL child surface at (1856,16)
inside the reference window. Its purpose is to compare fullscreen scanout with
composited presentation, keeping the main pattern, resolution and display refresh
the same. The child is synchronized to the parent and uses the same EGL context;
no capture scaling, filtering or pixel conversion is added. It is a diagnostic
control, not a production workaround. The compositor decides the actual path:
verify `zeroCopyPresented` in the source summary and the per-run presentation flag
histogram before treating a run as a composited control. The default source has
no child surface. CLI permits either ordering of `--seconds` and
`--composition-control`, and rejects duplicate options.

`wp_presentation` timestamps count actual presented content updates, not swaps or
frame callbacks. The trace includes timestamps, refresh period, sequence and
flags. `source.summary` reports presented/discarded/invalid/unresolved counts,
presentation-clock ID, rate and interval p50/p95/p99/max. Percentiles cover a
bounded prefix of 8192 intervals; `percentileOverflow` reports omitted samples.
The measured FPS covers first-to-last presentation, excluding startup and final
teardown. NVIDIA selection is process-local; the renderer is printed to stderr.
This does not change Mutter's rendering GPU or prove a zero-copy compositor path.

## Capture Controls

Launch the source with `--seconds 300`, `WAYLAND_DEBUG=1`, and separate stdout/log
files. While it remains alive, run the existing helper in another terminal:

```sh
python3 scripts/capture_isolation.py \
  --probe build/capture-and-nvenc/host/capture/cloudplay_linux_capture_probe \
  --source-pid SOURCE_PID --source-log SOURCE_LOG \
  --output build/presentation-capture-controls
```

Replace `SOURCE_PID` and `SOURCE_LOG` with the live source PID and trace path.
The output directory must not exist. Select the same monitor in both portal
dialogs and leave the source visible for each full 30-second run. No PNGs or video
are saved. The existing helper understands the generated presentation trace.
Its `validControl` means capture completed with balanced buffer ownership, not
that FPS acceptance or pixel correctness passed. Source evidence covers the
probe launch/portal/capture span, not precisely the first-to-last capture interval.
Window comparison uses the capture probe's existing `--source window` option.
The helper also accepts `--source-target window --snapshot-node` to run both
window controls and collect each source node's live parameters at startup,
10 seconds and 20 seconds after observing the source event. Each run is bounded
to three snapshots, with unique output files and actual observation times in
`nodeSnapshots`. These times are relative to source discovery, not first frame.
Snapshots run in the supervisor process; each attempt is bounded to seven seconds.
Reports include
`nodeSnapshotStatus` even when the tool, session access, or output validation
fails. Snapshot failure does not turn a completed capture into a performance pass.
Source feedback includes a deduplicated flag histogram and zero-copy presentation
count; these describe source presentation, not the capture buffer path.

## Lifetime and Failures

One owner thread dispatches callbacks. Feedback lives in 64 stable slots; a full
pool fails explicitly. Each resolved proxy is destroyed once; outstanding proxies,
frame callbacks and EGL objects are destroyed before the Wayland connection.
The optional child EGL surface/window/subsurface are also released before their
parent Wayland surface and context teardown completes.
Pending-feedback counts in the summary precede destructor cancellation. SIGINT,
SIGTERM and window close stop submission and return 130. Normal timed completion
drains feedback for up to one second. Missing display, unsupported protocols,
invalid geometry, disconnect, allocation or EGL errors fail with diagnostics.
Registry/configure waits are bounded at five seconds. The external `timeout`
also bounds a graphics-driver call that might block independently of our poll loop.
No keyboard shortcuts or synthetic input are installed.

Tests cover timestamp overflow, rollover, duplicate/nonmonotonic feedback,
interval statistics, CLI validation and missing-display startup cleanup.
Live checks covered normal timed cleanup and SIGTERM cleanup. Fake compositor
fault injection and Windows compiler verification of the new pure header remain
pending; these are not implied by the headless tests.

## Measured 2026-10-05

Source-only: 1792 presented frames, 59.8024 FPS over a 29.9486-second presentation
span in the requested 30-second run; no discarded/invalid/pending feedback.
NVIDIA RTX 3050 renderer, refresh feedback 16.665684 ms, flags intersection 7.

With capture, source presentation remained 59.9119 FPS during the GPU probe span
and 59.9429 FPS during the CPU probe span. GPU capture delivered/released 1103
frames in 30.0002 seconds: **36.7664 FPS**, minimum interval **33.9759 FPS**.
CPU capture delivered/released only 3 frames in 30.0003 seconds: **0.0999991 FPS**.
Both had zero local discards, sequence gaps or outstanding buffers.

Both negotiated SPA BGRx (8), 1920x1080, one plane, stride 7680, offset 0,
size 8294400, linear modifier, 0/1 framerate with maximum 60 FPS. GPU used DMA-BUF
and EGL import; CPU used MemFd. GPU fence p99 was 5.38975 ms, import p99 0.314853 ms
and buffer-held p99 5.63005 ms. These do not support increasing the buffer pool.

This rules out an unmeasured slow source as the sole explanation for these runs;
it does not establish the exact Mutter/PipeWire cause. Next isolate monitor vs
window capture and inspect live node parameters while source feedback continues.
Investigate the CPU delivery stall separately. Do not modify rate/pool/latency
properties without evidence, and do not interpret source FPS as capture success.
No captured-frame encoder or WebRTC integration is authorized by these results.
Evidence: `linux-capture-presentation-controls-2026-10-05.json`; raw local traces
and probe logs: `build/wayland-motion/`.

### Window Follow-Up

The presentation-verified window comparison on 2026-10-05 completed both 30-second
runs: GPU 39.9988 FPS (1200 frames, minimum interval 35.9705 FPS), CPU 41.4321 FPS
(1243 frames, minimum interval 36.9931 FPS). Source presentation across the probe
spans was 59.8919 and 59.4181 FPS respectively. All received frames were delivered
and returned, with no local discards, sequence gaps or outstanding buffers.
GPU fence/held p99 was 9.84944/10.0867 ms; CPU held p99 was 0.03463 ms.

Both live snapshots succeeded: the GNOME source was running, negotiated BGRx
1920x1080, framerate 0/1 and maxFramerate 60/1. Node snapshots do not establish
the runtime scheduling quantum. Installed Mutter: 50.0-1.fc44.
CPU window capture did not reproduce the monitor CPU stall. This narrows that
stall to monitor delivery or run-specific conditions, without establishing the
exact cause. GPU and CPU window capture both remain below the acceptance gate.
Next compare monitor node state during the stall and investigate compositor
frame scheduling/rate limiting before changing capture negotiation.
Evidence: `linux-capture-window-controls-2026-10-05.json`.

### Monitor Stall Follow-Up and Refresh Rate

With the display still at 1920x1080@60.003536 Hz, scale 1, non-VRR, the CPU monitor
retry delivered five frames in 30.001 seconds (0.166661 FPS), then stalled. All five
frames were returned. Snapshots at approximately 0, 10.07 and 20.04 seconds showed
the same node/object serial still running. Source presentation across the probe
span remained 59.8961 FPS. The full source trace contained 3366 presentations,
3166 with the zero-copy flag. This trace includes portal selection and is not
exactly aligned to capture timestamps. Evidence:
`linux-capture-monitor-stall-2026-10-05.json`.

Mutter 50's [rate check](https://github.com/GNOME/mutter/blob/50.0/src/backends/meta-screen-cast-stream-src.c#L1339)
rejects a frame when its timestamp interval is shorter than the negotiated maximum
rate permits. Its [monitor source](https://github.com/GNOME/mutter/blob/50.0/src/backends/meta-screen-cast-monitor-stream-src.c#L217)
handles the pre-paint scanout path only for DMA-BUF capture; CPU capture depends on
other paint/idle callbacks. These are relevant hypotheses for pacing and the CPU
stall, not proof of the exact cause on this machine.

Keep 60 Hz as the reference measurement. Test 120 Hz and 144 Hz separately with
the capture request still at 60 FPS and VRR disabled, recording actual negotiated
rate and source feedback each time. 120 Hz is nominally an integer multiple of
60; 144 Hz is not, and a higher refresh rate does not guarantee higher capture FPS.
The source follows display callbacks, so changing refresh also changes its own
submission cadence. Do not merge measurements from different refresh settings or
claim the higher-rate comparison isolates only compositor scheduling.

### Composition Control Results (Reviewed 2026-10-06)

The `--composition-control` source-only smoke presented 181 frames at 60.0032 FPS
with no zero-copy presentation feedback. The paired monitor runs then delivered:

| Measurement | GPU | CPU |
| --- | ---: | ---: |
| Capture duration (seconds) | 30.0005 | 30.0008 |
| Capture FPS | 38.266 | 41.2655 |
| Minimum interval FPS | 34.9936 | 38.9583 |
| Received/delivered/returned frames | 1148 | 1238 |
| Buffer-held p99 (ms) | 6.89913 | 0.025859 |
| Source presentation FPS across probe span | 59.0952 | 59.9169 |

All six node snapshots showed running sources. Both negotiated BGRx, 1920x1080,
stride 7680, offset 0, one plane, size 8294400, framerate 0/1 and maximum 60/1.
GPU used linear DMA-BUF/EGL import, CPU used MemFd. Both had zero local discards,
sequence gaps and outstanding buffers. GPU fence p99 was 6.59625 ms. No PNG or
video was saved, so this run is not new pixel-correctness evidence.

Neither source trace reported zero-copy presentation. Source feedback spans include
portal selection, not just the exact capture interval. The CPU monitor stall did
not recur with this composition control, which supports a presentation-path
dependency but does not establish an exact upstream cause. The child surface is
diagnostic source content, not a production capture workaround. Both capture paths
still fail the 60 FPS gate. No pool/rate changes or encoder integration follow
from this result. Next compare refresh/rate scheduling under controlled conditions.

Evidence: `linux-capture-composition-controls-2026-10-05.json`; raw local traces:
`build/ci-foundation-check/composition-controls-20261005/`.

# Fedora GNOME/Wayland Capture

## Refresh Investigation: 2026-10-06

All runs used the composited NVIDIA motion reference and requested 1920x1080/60.
Temporary display changes were restored to 60.003536 Hz afterward.

| Experiment | Result |
| --- | --- |
| Original maximum, exact framerate 60/1 at 60.0035 Hz | No formats; zero frames |
| Original maximum, range framerate at 119.877 Hz | 30.7658 FPS |
| Original maximum, range framerate at 144.003 Hz | 48.0317 FPS |
| Original maximum at 59.9339 Hz | No formats; zero frames |
| Fixed maximum negotiation at 60.0035 Hz | 38.1656 FPS |
| Fixed maximum negotiation at 59.9339 Hz | 30.0331 FPS |

The original maxFramerate was an exact 60/1, incompatible with sources advertising
a lower upper bound. It is now a positive range [1/1, requested FPS], preferred
requested FPS, allowing nominal 59.94 Hz sources while never exceeding the request.
The framerate range and explicit --fixed-rate experiment remain unchanged.
SPA's actual filter/fixate API is tested against variable-rate producers at
60000/1001, 60 and 144 Hz; exact framerate correctly rejects a 0/1-only producer.
This fixes negotiation compatibility, not capture throughput.

All completed controls ran for at least 30 seconds with balanced buffer leases,
zero local discards/sequence gaps and no capture-module pixel copies. None passed
the performance gate. At 144 Hz, producer intervals were approximately 20.833 ms,
consistent with delivery every third refresh, but this is not proof of the exact
upstream scheduling cause. Keep source timing and capture timing separate.

The acceptance predicate now rejects unbalanced received/delivered/released
counts and nonfinite timing values. Pixel correctness is still a separate gate.
The isolation runner supports --control gpu and --fixed-rate for single-variable
experiments; reports record the requested choice.

Fresh build: build/capture-fps-fix; all 45 CTests passed, including 18 Python
isolation tests. Changed C++ passed GCC -fanalyzer; formatting checks passed.
The standalone motion binary used for live runs was previously built and unchanged.
Windows compilation was not performed. NVENC implementation/tests were not modified.

Evidence: [refresh controls](linux-capture-refresh-controls-2026-10-06.json).
Remaining: compositor-side scheduling traces or a controlled compositor/version
comparison; reliable CPU delivery during direct scanout; fresh pixel verification
and sustained GPU acceptance before captured-frame NVENC interop or WebRTC.

## Presentation-Verified Control

The optional native [Wayland motion source](wayland-motion-source.md) now measures
actual compositor presentation independently of capture. The 2026-10-05 NVIDIA
source stayed near 59.9 FPS during both monitor controls; GPU capture measured
36.77 FPS and CPU MemFd delivery stalled after three frames. The 1080p60 gate
still fails. Next isolate source-node delivery and monitor/window behavior, not
source rendering speed or speculative buffer-pool changes. No encoder/WebRTC
integration was added. The older FFplay controls below remain historical evidence.

## CPU/GPU Isolation Controls

The motion reference now has an explicit NVIDIA launcher. Run
`WAYLAND_DEBUG=client sh scripts/nvidia-motion-reference.sh 2> /tmp/cloudplay-source.log`.
It selects Wayland/OpenGL, NVIDIA PRIME offload and NVIDIA's EGL vendor file
for FFplay only, removes conflicting `DRI_PRIME`, and fails if the vendor file or
FFplay is unavailable. It does not change GNOME, display routing or system defaults.
PRIME selection follows the [NVIDIA driver guide](https://docs.nvidia.com/datacenter/tesla/driver-installation-guide/optimus-laptops-and-multi-gpu-desktop-systems.html);
the vendor override is implemented by [libglvnd](https://github.com/NVIDIA/libglvnd/blob/master/src/EGL/libeglvendor.c).

October 3 smoke verification: FFplay PID13658 initialized OpenGL and appeared
in `nvidia-smi pmon` as graphics type G, with SM25/34% and memory3/4% across two
samples. The input was limited to 12 seconds; FFplay stayed open after input
ended and was explicitly stopped with SIGINT. This verifies active NVIDIA
usage, not presentation FPS or capture acceptance. Capture's existing importer
already explicitly selects an NVIDIA EGL/CUDA device. The completed NVIDIA-source
controls are recorded below; the earlier CPU-only result used the Intel source.

The timing probe accepts `--cpu-capture` without snapshots. It negotiates mapped
MemFd/MemPtr packed RGB and observes frame cadence without EGL import, DMA-BUF
fence waits, or diagnostic pixel copying. This changes the compositor's buffer
path too; it is not a measurement of removing only the fence wait. CPU mode always
reports `stable1080p60Gpu:false` and exits 3 after a completed timing run. Use
`--capture-diagnostics --cpu-capture` separately for CPU pixel snapshots.

```sh
build/capture-and-nvenc/host/capture/cloudplay_linux_capture_probe --seconds 30 --source monitor --timing-diagnostics
build/capture-and-nvenc/host/capture/cloudplay_linux_capture_probe --seconds 30 --source monitor --timing-diagnostics --cpu-capture
```

`--buffer-pool 2..8` changes the preferred buffer count (default 4) while retaining
the accepted range 2..8. It does not force the producer to allocate that count.
Summaries report requested limits, observed pool size/peak, dequeue batches,
multi-buffer batches, largest batch, released frames and maximum/outstanding
borrowed buffers. The reported pool is the last observed active pool before
stream destruction; it is not the number of ready buffers or a post-shutdown leak.
The observer uses the official PipeWire
[add/remove buffer events](https://docs.pipewire.org/structpw__stream__events.html).
Exception/shutdown tests simulate multiple borrowed frames and verify that all
returns are accounted for, with zero outstanding frames after release.

`scripts/capture_isolation.py` runs matching 30-second GPU and CPU monitor controls,
retains stdout/stderr separately in a new private directory, and writes a JSON
report. Source evidence comes from the source application's own log and open DRM
descriptors. An open DRM device is access evidence, not proof of the rendering GPU.
Reports also preserve requested-rate, negotiated-format/buffer events and all
per-second intervals, including zero-frame intervals, without depending on raw logs.
Use `--control cpu` to repeat only the CPU baseline; the default is `--control both`.
A CPU-only run cannot authorize a pool experiment or GPU acceptance, even at 60 FPS.
Wayland presentation feedback is deduplicated and validated; absent feedback does
not establish presentation FPS. Feedback measures surface updates, not pixel-unique
video content. A missing/exited source, early session closure or unreturned buffer
cannot qualify as a completed control. Each subprocess has a 170-second deadline.
The runner checks source process starttime and state every 100 ms, rejecting
zombies and PID reuse. Source exit, timeout or interruption terminates and reaps
the probe (forced termination after two seconds if necessary). Exit codes 125,
124 and 130 respectively mark these exclusions. Reports are atomically saved
after each run, including failures; an invalid baseline stops further portal
requests. A probe launch failure is recorded as exit 126.

Example setup in a normal Wayland session (replace PID with the running FFplay PID):

```sh
WAYLAND_DEBUG=client sh scripts/nvidia-motion-reference.sh 2> /tmp/cloudplay-source.log
python3 scripts/capture_isolation.py --probe build/capture-and-nvenc/host/capture/cloudplay_linux_capture_probe --source-pid PID --source-log /tmp/cloudplay-source.log --output /tmp/cloudplay-isolation-new
```

Run FFplay in one terminal and the helper in another. Select the same monitor and
keep motion visible in both dialogs/runs. Wayland debug logging can affect source
timing; retain it for both controls and report that limitation. The helper only
tries preferred pool size 8 if valid controls, source feedback >=59 FPS in both
runs with no discarded presentation feedback, CPU
capture >=59 FPS, GPU capture <59 FPS, GPU fence/hold p99 >=15 ms and an observed
pool below 8 support that experiment. This is a hypothesis, not an automatic fix;
compare the actual pool count and full acceptance results afterward.

Automated check: `python3 -m unittest discover -s scripts/tests`. This covers duplicate/malformed
feedback, descriptor ambiguity, missing processes, partial/error logs and experiment
eligibility, PID reuse/zombies, subprocess cleanup and interrupted report persistence.
These fourteen Python tests also run through CTest when Python is available; live
acceptance for these controls remains a separate user-selected source test.
Keep the source running until the helper finishes, then close it manually. A
fixed-duration source can expire while the portal is waiting for consent.

## Cadence Isolation Experiments

### NVIDIA-Source Dual Controls

October 3 controls used the NVIDIA launcher, same requested monitor/range/pool4,
and an indefinitely running source. FFplay PID14568 was active in NVIDIA pmon
as graphics type G, with SM48%/memory7% in both samples before capture. Both
runs completed without source exit or any local drops, sequence gaps, outstanding
buffers or application pixel copies.

| Path | Duration | Average FPS | Minimum interval FPS | Frames returned |
| --- | --- | --- | --- | --- |
| DMA-BUF / EGL | 30.0009s | 38.8321 | 36.9702 | 1165 |
| Mapped MemFd | 30.0001s | 37.2332 | 32.9726 | 1117 |

Both negotiated 1920x1080 BGRx, stride7680, offset0, size8294400, full crop and
no transform metadata. GPU modifier was linear, with ready implicit fences and
successful EGL imports. GPU fence mean/p99 was4.657/5.979ms; held p99 was7.335ms.
CPU had no EGL import/fence wait, and held p99 was0.027083ms. Producer interval
p50 was33.321ms GPU and33.141ms CPU. Matching the source GPU did not achieve60FPS;
these results do not support a pool-size experiment or bypassing fences.

The new scoped snapshot tool was exercised during CPU capture:

```sh
python3 scripts/pipewire_node_snapshot.py --node NODE --output /tmp/capture-node-new.json
```

Use the actual `nodeId` from `capture.source`, while sharing is active. This queries
`pw-dump -N NODE` with a five-second timeout, validates the node, saves exclusively
with mode0600 and whitelists timing/format properties and SPA parameters. Each
parameter list is limited to32 entries with truncation reported. It does not change
properties or dump unrelated applications. Missing nodes, restricted remote access,
missing tools, invalid output and existing files fail with JSON diagnostics. The
default remote may not expose a portal-scoped node. Running pw-dump adds some
system load; the CPU timing run included one query and is not an uninstrumented run.

Live node70 was running, driver/want-driver true, `Stream/Output/Video`. Its fixed
SPA Format was BGRx 1920x1080, framerate0/1 and maximum60/1. Advertised Latency
fields were zero, ProcessLatency empty, and node.rate/node.latency absent. None of
these establishes actual clock quantum, producer pacing or60FPS presentation.
Source presentation feedback remains absent; origin latency and pixel correctness
for these timing runs are unverified. Next: independently measure source presentation
and compositor cadence. Encoder/WebRTC acceptance stays closed.

Full report and node snapshot:
[`linux-capture-nvidia-controls-2026-10-03.json`](linux-capture-nvidia-controls-2026-10-03.json).

### Completed CPU-Only Retry

The October 3 retry (`--control cpu`) completed 30.0001 seconds, with the source
alive and the user confirming continuously visible fullscreen motion. It captured
137 frames (4.56665 FPS), all returned, with zero local discards, sequence gaps or
outstanding buffers. Minimum one-second interval was 0 FPS. Negotiation was SPA
BGRx / DRM XRGB8888, mapped MemFd, stride 7680, offset 0, size 8294400 bytes,
full crop and no transform metadata. No EGL import, application fence wait or
pixel copying occurred. Buffer-held p99 was 0.035486 ms; poll p99 was 1.3951 ms,
while arrival gaps reached 12034.3 ms. Source presentation feedback was absent,
so user confirmation is not an independently measured presentation rate.

This is a completed lifecycle control, not a passing performance control. CPU
negotiation changes the producer path too; these results do not prove that GPU
fences are irrelevant. They do not support increasing the buffer pool. Next:
measure source presentation and inspect compositor/PipeWire node cadence under
both memory paths. No encoder/WebRTC integration has been started.
Artifact: [`linux-capture-cpu-control-2026-10-03.json`](linux-capture-cpu-control-2026-10-03.json).

### October 3 Live Isolation

The updated runner completed a GPU monitor control for 30.0002 seconds at
38.8664 FPS (minimum one-second interval 34.7966 FPS). All 1166 frames were GPU
imported and returned, with zero local discards, sequence gaps, outstanding
buffers or application copies. The display remained at 1920x1080, 60.0035 Hz,
non-VRR. Negotiation was SPA BGRx / DRM XRGB8888, linear DMA-BUF, one plane,
stride 7680, offset 0, allocation 8294400 bytes, full crop, no transform metadata,
implicit fences ready and EGL import successful. Fence mean/p99 was 4.721/7.747
ms; buffer-held mean/p99 was 5.138/8.044 ms. Acceptance remains false.

The CPU run received and returned four frames, then was interrupted at 10.2027
seconds when FFplay exited. Its Wayland log records an Escape key event before
shutdown. The runner terminated/reaped the probe, saved the partial result and
excluded it with code 125 (`source_exited_or_replaced`). Its FPS is not a valid
control comparison. No pool-size experiment was attempted. The user deferred
retry; source and probe processes have exited.

Neither run supplied source presentation feedback; actual source presentation
rate, active rendering GPU and pixel correctness for these timing runs remain
unverified. Results are retained in
[`linux-capture-isolation-2026-10-03.json`](linux-capture-isolation-2026-10-03.json).
Raw stdout/stderr and the source log are referenced there as local `/tmp` paths,
which are temporary. Do not remove fences or begin encoder/WebRTC integration
based on these results. Next: repeat the CPU control with the source kept visible
and running, and obtain independent source presentation evidence.

The probe accepts `--timing-diagnostics` and `--fixed-rate`. Both default off.
The baseline advertises a framerate range [0/1, 60/1] and a maxFramerate range
[1/1, 60/1], preferring 60/1 (before 2026-10-06, maxFramerate was exact 60/1);
`--fixed-rate` advertises an exact 60/1 fraction. This is a negotiation experiment,
not a guarantee that Mutter produces 60 FPS. Requested and negotiated rates are
reported separately. SPA POD construction follows the
[PipeWire POD API](https://docs.pipewire.org/group__spa__pod.html).

```sh
build/capture-and-nvenc/host/capture/cloudplay_linux_capture_probe --seconds 30 --timing-diagnostics --source monitor
build/capture-and-nvenc/host/capture/cloudplay_linux_capture_probe --seconds 30 --timing-diagnostics --source monitor --fixed-rate
build/capture-and-nvenc/host/capture/cloudplay_linux_capture_probe --seconds 30 --timing-diagnostics --source window
build/capture-and-nvenc/host/capture/cloudplay_linux_capture_probe --seconds 30 --timing-diagnostics --source window --fixed-rate
```

`--source monitor|window` limits the consent dialog to that source type after
checking `AvailableSourceTypes`. Default `any` keeps the existing monitor/window
selection. The probe logs requested source bits, optional returned `source_type`
(zero means unavailable on this portal), and the node ID. Unexpected returned
source types are rejected with session cleanup. These fields follow the
[ScreenCast portal API](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.ScreenCast.html).
Use the same continuous-motion 1920x1080 source and non-VRR 60 Hz
display mode for all four runs. Keep sharing active until the summary. Neither
command performs PNG readback. Window capture must negotiate 1920x1080 without
scaling to qualify. The existing strict acceptance gate remains unchanged.

`capture.timings` reports milliseconds: count, overflow, min/max/mean,
nearest-rank p50/p95/p99 and histogram bins with exclusive upper bounds
8, 15, 18, 25, 35, 50 ms and an unbounded final bin. Arrival intervals use
successful dequeue times, including drained buffers; producer intervals use
valid, increasing, continuous PTS on the selected newest buffer. Dequeue is
consumer arrival, not the instant Mutter generated a frame. Presentation age
now uses CLOCK_MONOTONIC sampled at dequeue, before fence waits.

Fence samples cover each plane's implicit writer-fence poll. Import, callback,
EGL destruction, and queue-call durations are separate. Buffer-held time covers
the selected buffer from dequeue through image destruction and return, including
failure paths; immediately discarded drain buffers have queue timing only.
Poll intervals include consumer work, the probe's 1 ms sleep and scheduling;
startup poll intervals are included. These timings do not measure GPU execution
or compositor work hidden behind driver calls.

All collection is on the capture owner thread, with storage reserved before
capture: 8192 samples per stage and 131072 poll intervals. Aggregates/histograms
cover all observations; percentiles cover the retained prefix, with overflow
reported explicitly. No per-frame timing output or allocation occurs. Summaries
are calculated after stop and survive shutdown; restart resets them. Timing
tests cover invalid samples, exact bin boundaries, percentiles, bounded overflow,
reset and exception cleanup. SPA tests inspect fixed/range rate PODs directly.
Timing mode also logs bounded, JSON-escaped PipeWire backend errors (up to 255
bytes, truncation reported), error object/sequence, stream state, requested rate,
source, and received/released/delivered counts on failure. The first fatal error
is preserved instead of being replaced by a subsequent disconnect. Closing
sharing reports `portal.session_closed`; it is not a successful duration test.
No new 1080p60 acceptance is claimed; measured experiment results follow below.

### Measured Results, 2026-10-02

GNOME Mutter GetCurrentState confirmed eDP-1 at 1920x1080, scale 1,
60.003536 Hz, non-VRR. Installed Mutter/GNOME Shell are 50.0-1.fc44;
PipeWire is 1.6.9-1.fc44. NVIDIA reports RTX 3050 6GB Laptop GPU with
driver 615.71.09. These runs used fullscreen FFplay testsrc2 requested at 60 FPS,
restarted between runs. The reference application's actual presentation rate
and renderer have not been independently verified. GPU device and D-Bus queries
require session/device access outside the sandbox.

| Source / Rate | Duration | Average FPS | Minimum Interval FPS | Discards / Gaps | Result |
| --- | --- | --- | --- | --- | --- |
| Monitor / range | 30.001 s | 39.5653 | 35.9831 | 0 / 0 | Exit 3; performance gate failed |
| Monitor / fixed 60/1 | No frames | N/A | N/A | N/A | Exit 1; `no more input formats` |
| Window / range | 30.0074 s | 40.8900 | 38.9583 | 0 / 0 | Exit 3; performance gate failed |
| Window / fixed 60/1 | No frames | N/A | N/A | N/A | Exit 1; `no more input formats` |

Successful range runs negotiated SPA BGRx (8), DRM XRGB8888, 1920x1080,
one linear DMA-BUF plane (modifier 0), stride 7680, offsets 0 and allocation
8294400 bytes. Actual portal source types were monitor (1) and window (2).
Both imported every delivered frame on NVIDIA, with no capture CPU frames,
CPU readbacks, CPU copies, GPU copies, local discards or sequence gaps.
Negotiated framerate was unspecified (0), maximum 60. No PNG or encoded frame
was produced during these timing runs.

Monitor: 1187 frames; producer interval mean 25.2796 ms, p50 33.32 ms,
p99 33.338 ms. Most producer intervals were approximately 16.7 or 33.3 ms.
Fence waits averaged 4.61731 ms (p99 5.25868), import 0.392207 ms,
and selected-buffer hold 5.04830 ms (p99 5.87628).
Window: 1227 frames; producer interval mean 24.4771 ms, p50 21.767 ms,
p99 33.710 ms. Fence waits averaged 11.1184 ms (p99 17.1486), import
0.350603 ms, and hold 11.5100 ms (p99 17.5034). No timing sample overflow
occurred. These distributions describe different runs, not paired measurements.

The first window selection attempt timed out at portal Start. Another range
attempt delivered 643 frames before session closure; it is explicitly excluded
from the duration comparison. The initial fixed monitor attempt reported generic
PipeWire -32; a retry with improved diagnostics preserved the earlier stream
failure `no more input formats`, matching the fixed window attempt. Neither
fixed-rate failure is a performance result. The source format incompatibility
rules out using this exact-fraction offer as a pacing fix on this configuration.

No evidence yet distinguishes source presentation/damage cadence from compositor
scheduling or GPU fence/backpressure. Zero local discards/gaps establishes that
the consumer did not locally drop the received stream; it does not prove the
producer was never throttled. Window fence tails reach the 16.7 ms frame budget,
so the next investigation should verify source presentation cadence and renderer,
then isolate synchronization/backpressure with one controlled change at a time.
Keep the acceptance gate and synchronization guarantees intact.

Full timing/summary/failure events are retained in
[`linux-capture-cadence-2026-10-02.json`](linux-capture-cadence-2026-10-02.json).
The probe's GPU import validates EGL image creation, not texture sampling or NVENC
registration. No WebRTC or captured-frame encoder integration was advanced.

## Source And Recycling Controls

The duration probe accepts `--cpu-capture` and `--buffer-pool 2..8`. CPU mode
uses mapped MemFd/MemPtr frames with the same source, duration and rate offer.
It always reports `stable1080p60Gpu:false` and exits 3 on a completed diagnostic
run. The default pool preference remains four with range 2..8.

Keep the same moving source visible and alive across both runs. Record FFplay
stderr with `-stats -loglevel verbose`; `fd=` reports source frame drops.
DRM driver/PCI fields in `/proc/PID/fdinfo` identify open GPU devices; verify
which GPU is rendering with engine activity or driver process diagnostics.
Capture's NVIDIA EGL renderer alone does not establish FFplay's rendering GPU.

Actual screen presentation requires a source requesting `wp_presentation.feedback`.
Collect `WAYLAND_DEBUG=client` separately from timing controls because protocol
logging adds overhead. The helper reads feedback timestamps and discarded events;
FFplay/SDL versions without feedback leave presentation FPS unknown. Generated
timestamps, frame callbacks and zero decoder drops do not prove distinct screen
presentations. Feedback also does not verify distinct pixel content.

```sh
python3 scripts/capture_isolation.py \
  --probe build/linux-capture/host/capture/cloudplay_linux_capture_probe \
  --source-pid SOURCE_PID --source-log /tmp/source.log \
  --output /tmp/capture-isolation-new
```

Select the same 1920x1080 monitor in each consent dialog, at non-VRR 60 Hz and
scale 1. The helper saves 30-second GPU/CPU JSONL, stderr, per-run source-drop
deltas and a report in a new directory. Range negotiation is retained. A source
exit invalidates the control. Verify continuous motion and matching negotiated
resolution/rate ceilings before comparing. CPU producer cadence includes any
compositor readback cost; it is not the source application's presentation rate.

`bufferPoolSize` counts actual allocation callbacks; `maxBufferPoolSize` retains
the peak. Requested preference/bounds are separate. `outstandingBuffers` counts
received minus successfully returned buffers; its peak is sampled immediately
after every dequeue, before returning older drain buffers. `dequeueBatches`,
`multiDequeueBatches` and `maxDequeueBatch` report successful drain batches.
Interval events expose live pool/outstanding counts. Callbacks run on the owner
thread. Stop removes the listener before destruction, retaining the last observed
pool size for the final summary.

The helper tests a pool preference of eight and repeats both controls only when
source feedback and CPU capture reach 59 FPS, GPU capture is below 59, the actual
pool is below eight, and fence/held-buffer p99 both reach 15 ms. This supports a
recycling experiment, not a finding of pool exhaustion. Compare actual allocation
and cadence afterward. Short holds with one outstanding buffer point toward
source/compositor investigation. A source renderer change needs fresh GPU and CPU
controls with the same test pattern, monitor, range offer and pool request.

Fence waits, EGL cleanup/return ordering and acceptance thresholds are unchanged.
NVENC remains gated on stable GPU capture plus separate pixel/motion validation;
the helper leaves NVENC readiness false and WebRTC pending.

### Isolation Measurements, 2026-10-02

The source-renderer experiment preserved FFplay testsrc2, 1920x1080, the same
monitor, pool preference four and range negotiation. Intel-source DRM fdinfo
identified `xe` at `0000:00:02.0`. NVIDIA-source selection used
`__NV_PRIME_RENDER_OFFLOAD=1` and NVIDIA's EGL vendor JSON; `nvidia-smi pmon`
confirmed FFplay PID 18561 as an active NVIDIA graphics process (48% SM, 8% memory).
Protocol logging was disabled for these timing controls.

| Source Renderer / Capture | Average FPS | Producer Mean ms | Fence p99 ms | Held p99 ms | Validity |
| --- | --- | --- | --- | --- | --- |
| Intel / DMA-BUF | 39.3988 | 25.4008 | 9.06584 | 9.43154 | Completed 30-second capture |
| Intel / CPU | 2.39997 | 46.3712 | No fence wait | 0.061861 | Excluded: source exited during control |
| NVIDIA / DMA-BUF | 38.6659 | 25.8829 | 11.5762 | 11.9658 | Completed 30-second capture |
| NVIDIA / CPU | 12.5996 | 29.1351 | No fence wait | 0.035277 | Excluded: source exited during control |

Both GPU runs allocated four buffers, held at most one, dequeued batches of one,
and reported no local discards or sequence gaps. Observed FFplay drop-counter
deltas were six (Intel GPU run) and ten (NVIDIA GPU run); these deltas include
portal startup, so they are not an exact 30-second source-drop rate.
Matching the source renderer to NVIDIA did not remove the observed capture limit.
These monitor runs provide no evidence of pool exhaustion; they do not rule out
compositor-side throttling or explain the earlier window fence tails.

FFplay/SDL emitted no presentation feedback in the separate protocol trace.
Actual source presentation FPS and distinct frame content remain unverified.
The CPU runs cannot distinguish producer cadence from synchronization because
continuous-motion controls were interrupted. Both corrected controls encountered
portal Start timeout before any frames arrived. These failures are retained rather
than counted as successful controls. No source/compositor/synchronization cause
has been established and no stable/pixel-correct GPU acceptance is claimed.

Full events and source evidence for the renderer experiment are retained in
[`linux-capture-isolation-2026-10-02.json`](linux-capture-isolation-2026-10-02.json).
NVENC and WebRTC remain gated/pending.

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
The user subsequently confirmed that scrolling, window dragging, cursor movement
and gradients looked correct. This is visual acceptance, not an independent
pixel-exact desktop reference or an automated temporal test.

The follow-up no-readback run lasted 30.0001 seconds: 974 received/delivered GPU
frames, 32.4666 FPS, minimum interval 19.9997 FPS, zero local discards/sequence
gaps, zero diagnostic or capture-module CPU copies and zero reported GPU copies.
It used the same format/layout, with the ordinary probe's hidden cursor. Mean
producer interval was 30.8136 ms (maximum 499.975 ms); mean EGL import was
0.263963 ms (maximum 0.591831 ms). Of 974 PTS samples, 971 were future-dated;
signed mean age was -7.07511 ms, not capture-origin latency. Maximum negotiated
rate remained 60, with actual negotiated rate unspecified. Exit 3 correctly
rejected sustained 1080p60. Low FPS persists without CPU readback; producer/
compositor pacing versus source damage cadence remains unresolved.

All 19 combined and five default CTests, clang-format 18 and GCC -fanalyzer pass.
All five [CI jobs](https://github.com/dahalujwal1000/CloudPlay/actions/runs/37011712241)
passed for desktop-validation source revision 1766c74c915911b9ccf65dd996d2b3ca7b608306.
Next is captured-buffer GPU/NVENC interoperability validation; an EGLImage is not
an NVENC resource. Development headers and a supported registration/synchronization
path must be established before production encoder implementation. NVENC
integration and WebRTC have not started; WebRTC remains gated on stable 1080p60.

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

## Diagnostic Negotiation Ceiling

The probe accepts `--diagnostic-max-fps 60..65` (default 60). This changes only
the SPA maxFramerate offer; the nominal capture target remains 1920x1080/60.
It cannot be combined with `--fixed-rate` at a non-default ceiling. Runs with
a non-default ceiling always report `stable1080p60Gpu: false`, even if cadence
improves; an experiment is not production acceptance.

Example controlled comparison, with the same continuous-motion source and
display mode for both runs:

```sh
build/capture-fps-fix/host/capture/cloudplay_linux_capture_probe --seconds 30 --source monitor --timing-diagnostics
build/capture-fps-fix/host/capture/cloudplay_linux_capture_probe --seconds 30 --source monitor --timing-diagnostics --diagnostic-max-fps 61
```

Record the actual negotiated maximum, not just the requested ceiling: the
producer can cap it further. No scaling, frame duplication, pixel conversion,
fence bypass, or display refresh changes are performed by this option.

Rationale: [Mutter 50.0 source](https://raw.githubusercontent.com/GNOME/mutter/50.0/src/backends/meta-screen-cast-stream-src.c)
checks producer timestamps against an integer-microsecond minimum interval
derived from maxFramerate. Near-boundary scheduling is a hypothesis to test,
not a confirmed explanation for CloudPlay's low capture FPS.

2026-10-07: baseline delivered 38.1994 FPS (minimum interval 34.9801), while
the 61 ceiling control delivered 40.7987 FPS (minimum interval 36.973). Mutter
negotiated the latter maximum down to 60.0035. Both completed 30 seconds with
balanced GPU buffer counts, zero local discards/sequence gaps and zero reported
CPU/GPU copies. Neither passed. This small single-run difference does not
establish a causal improvement. Evidence: [ceiling control](linux-capture-ceiling-2026-10-07.json).

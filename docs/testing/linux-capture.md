# Fedora GNOME/Wayland Capture

## Build

The Linux backend is opt-in, preserving SDK-free domain builds and Windows capture.
The user installed the Fedora development prerequisites:

```sh
sudo dnf install pipewire-devel glib2-devel mesa-libEGL-devel libdrm-devel
cmake -S . -B build/linux-capture -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCLOUDPLAY_LINUX_CAPTURE=ON
cmake --build build/linux-capture
ctest --test-dir build/linux-capture --output-on-failure
build/linux-capture/host/capture/cloudplay_linux_capture_probe --seconds 30
```

Use the normal logged-in GNOME Wayland user, not sudo, SSH, a container or another
user's bus. Session D-Bus and GPU device access are required. Sandbox IPC/device
restrictions are distinct from driver failures. Linux capture plus NVENC readiness
builds run 15 CTest cases. No encoder, CUDA Toolkit or WebRTC is needed here.

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
No screen/game content is saved or streamed.

Gate: >=30 seconds, >=29 one-second intervals, average >=59 FPS, every interval
>=58 FPS, 1920x1080, all frames imported on NVIDIA, no local discards/sequence gaps,
no CPU frames/copies and available presentation timestamps. Exit 0: gate passed;
1: runtime failure; 2: bad CLI; 3: finished but not stable. Short smoke tests return
3; `--help` exits 0 without capture. Capture success alone is not NVENC acceptance.

## Metrics

JSON records include FPS, resolution, DRM FourCC/modifier, received/delivered/discarded
counts, sequence gaps, GPU imports, CPU frames, capture-module copy counts and
presentation age. FourCC 875713112 is XRGB8888 (BGRx bytes on this little-endian host).
Sequence gaps can include local discards; do not sum them as disjoint losses.
Drops before producer sequence assignment are not observable here.

SPA header PTS is compared with CLOCK_MONOTONIC at delivery. PTS is presentation
time, not guaranteed capture origin. Future PTS values are counted; signed ages
are reported rather than clamped to fake zero latency. Nonnegative age has a
separate mean/max/count. Missing/implausible timestamps are excluded; absent
means are null. Capture-origin latency is explicitly unverified. Internal driver,
compositor and cross-GPU copies are unknown. No pixel-content/readback test runs.

Failures report a typed reason, operation, native numeric error where available
and last frame metrics. Codes: 0 session unavailable; 1 permission/session closed;
2 timeout; 3 portal; 4 PipeWire; 5 unsupported format/buffer; 6 GPU import;
7 consumer. No handles, tokens, titles, pixels or raw external errors are logged.

## Tests

- Frame lease: exactly-once return, exception cleanup, image-before-buffer release.
- Gate: short, slow, CPU, wrong-size, dropped and missing-import streams rejected.
- Backend: owner thread, invalid target, absent session, repeated failure/start/stop.
- Private mock D-Bus portal: requests/responses, cancellation at Start, exactly-once
  session close and repeated stop/destruction without GNOME or a GPU.
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
The user agreed to switch temporarily to 60 Hz for another animated, uninterrupted
sample. That retest is pending. No WebRTC integration has started.

All 15 combined native tests passed locally with IPC access. Native formatting,
GCC analysis and all five [capture CI jobs](https://github.com/dahalujwal1000/CloudPlay/actions/runs/37005794506)
passed at revision `fb5bbbcbd5ed8dd6ff563ea2cfbbadaf4947e9a6`.

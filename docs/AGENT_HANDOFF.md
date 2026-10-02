# Coding Agent Handoff

Last verified: 2026-10-02.

## Current Status

TASK-001 foundation is verified across native host, Android, signaling, and CI,
including hosted Windows/MSVC build/tests. TASK-002 now has a Windows Graphics
Capture module and diagnostic; live GPU acceptance remains
unverified. The user changed the primary host to Fedora Linux (ADR-006).
Linux SDK interface setup and NVENC readiness diagnostics are now available;
live synthetic H.264/HEVC hardware checks pass. Linux portal/PipeWire capture is
implemented behind IFrameCapture and imports live 1080p DMA-BUFs on NVIDIA.
The sustained 1080p60 gate has NOT passed (observed 35-42 FPS). Production encoding,
captured-frame NVENC interoperability, streaming, and pairing are not implemented.

Read `AGENTS.md`, `docs/TASKS.md`, the relevant architecture documents, and ADRs
before continuing. Preserve the game-agnostic design and the existing subsystem
boundaries. Coordinate ownership using `docs/AI_AGENT_WORKFLOW.md`.

## Implemented

| Files | Purpose |
| --- | --- |
| `CMakeLists.txt`, `CMakePresets.json` | C++20, Ninja development build, CTest, compiler warnings treated as errors |
| `host/CMakeLists.txt` | Core and Telemetry libraries, host executable, three test registrations |
| `host/core/include/cloudplay/core/session.hpp`, `host/core/src/session.cpp` | Typed session states, events, failures, transition validation |
| `host/core/include/cloudplay/core/config.hpp`, `host/core/src/config.cpp` | Stream configuration defaults and validation |
| `host/telemetry/include/cloudplay/telemetry/logger.hpp`, `host/telemetry/src/logger.cpp` | Structured lifecycle logs with timestamps and optional failure codes |
| `host/app/main.cpp` | Command-line configuration and startup/shutdown smoke lifecycle |
| `host/tests/core_tests.cpp` | Lifecycle, recovery transitions, configuration boundaries, log record checks |
| `.clang-format`, `.editorconfig` | Initial formatting and editor conventions |
| `.clang-tidy` | Native analyzer rules |
| `android/` | Six modules, Kotlin lifecycle/configuration, transport contracts, Compose shell, tests, pinned toolchain/wrapper |
| `signaling/` | Authenticated loopback HTTP/WebSocket diagnostics, validated config/messages, bounds, logging, tests, lockfile |
| `.github/workflows/foundation.yml` | Linux/Windows native, native quality, signaling, and Android CI jobs |
| `docs/BUILDING.md`, `docs/tasks/TASK-001-foundation.md` | Build commands and acceptance criteria |
| `host/capture/` | Portable frame policy, Windows WGC/D3D11 implementation, diagnostic, validation tests |
| `docs/architecture/capture.md`, `docs/testing/capture.md` | Capture ownership and hardware acceptance checklist |
| `host/diagnostics/` | Optional Linux NVENC driver API readiness probe, no encode session |
| `docs/decisions/ADR-006-linux-host.md`, `docs/testing/linux-nvenc.md` | Linux-first decision, SDK setup and observed hardware checks |
| `host/capture/linux/`, `frame_capture.hpp`, `capture_acceptance.hpp`, capture tests | Owner-thread portal/PipeWire capture, NVIDIA EGL import, diagnostic, shutdown/gate tests |
| `docs/decisions/ADR-007-linux-capture.md`, `docs/testing/linux-capture.md` | GPU frame ownership, consent, diagnostics and sustained acceptance gate |

The build exposes `CloudPlay::Core`, `CloudPlay::Telemetry`, and on Windows
`CloudPlay::Capture`. Other host modules
remain planned; no placeholder implementation of hardware or external APIs exists.
CMake minimum is now 3.25, matching preset schema version 6.

### Behavioral Contracts

- Initial state is `OFFLINE`. The normal path is `STARTING -> READY`, optional
  `PAIRING -> READY`, then `CONNECTING -> CONNECTED -> STARTING_GAME -> STREAMING`.
- `GAME_STOPPED` returns `STARTING_GAME` or `STREAMING` to `CONNECTED`.
- `STOP` and `FAIL` move any active state to `STOPPING`. Disconnect while connecting,
  connected, or running a game also enters `STOPPING`.
- `STOPPED` moves `STOPPING` to `OFFLINE`. The orchestration owner must complete
  actual resource cleanup before emitting it; cleanup is not implemented yet.
- `FAIL` requires a failure code. Other events reject failure codes. Rejected
  events leave the state unchanged and return an empty optional.
- Session mutation is not thread-safe. The orchestration thread must serialize it.
- Stream defaults are 1920x1080, 60 FPS, 12 Mbps. Validation currently accepts only
  that resolution/frame rate and a bitrate from 6 through 20 Mbps.
- The executable accepts `--bitrate-mbps 6..20`. Bad arguments/configuration exit
  with code 2. Successful execution logs four transitions and exits.
- Logger accepts typed lifecycle fields rather than arbitrary messages. It locks
  each record; its output stream must outlive it. No metric collection exists yet.

## Verification

Executed successfully in this Linux workspace:

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

The original four Linux tests passed: `host.core`, `host.smoke`,
`host.invalid_config`, and `capture.frame_policy`; the default build now also
includes `capture.frame_lease`. `host.invalid_config` expects a failing exit code
for a zero bitrate. The unit executable
uses a small throwing assertion helper; it does not depend on a third-party test
framework. These tests do not verify hardware, resource cleanup, or live networking.

Additional successful local checks:

- Native formatting: clang-format 18 dry-run with warnings as errors.
- Native static analysis: GCC 16 `-fanalyzer` build. Clang-tidy 18 could not parse
  the local GCC 16 standard headers; CI checks it with Ubuntu's compatible toolchain.
- Signaling: `npm run check` (TypeScript, ESLint, Prettier, Node tests), plus direct
  execution of `dist/test/server.test.js` reporting seven passing tests.
- Android: clean debug APK assembly, JUnit core tests (four passed), app/UI lint
  (warnings as errors except pinned dependency/toolchain update notices), and Kotlin
  Spotless formatting check passed. APK: `android/app/build/outputs/apk/debug/app-debug.apk`.
- Gradle wrapper JAR checksum matches the official Gradle 8.13 checksum; distribution
  checksum is pinned in `gradle-wrapper.properties`.

The clean Android build/test/lint run passed after the final resource/module changes.
Foundation Windows/MSVC and all five GitHub jobs were verified passing at revision
`bd302dd937476f38c2b84e5c64ad370dfc44a1f9` in
[this CI run](https://github.com/dahalujwal1000/CloudPlay/actions/runs/36999404589).
A local Git repository exists; this agent did not manually commit or push. The user's
autosync tooling commits/pushes changes and triggers CI. Concurrently added
`scripts/` autosync tooling and `.vscode/` settings are outside this foundation work
and were not modified or activated by this agent.

### Signaling Boundary

`loadConfig` requires a random URL-safe bootstrap token; only loopback binds are
accepted. Every endpoint/upgrade requires a bearer header. `/health` reports service
health; `/v1/signaling` accepts only `service.status` version 1 with an empty payload
and UUID request ID. Response capabilities are empty. Limits: 4096-byte messages,
60 requests/upgrades per IP per minute, 60 messages per connection, 60-second socket
lifetime, 16 KiB outbound buffering check. Active sockets terminate on shutdown.

Pairing, SDP/ICE routing, session ownership/authorization, TLS, and persistent
credentials must be implemented before making this a remote service. The APK does
not connect to this bootstrap service. No credentials are embedded or stored.

### Android Boundary

Modules: app, core, networking, streaming, input, ui. The last three transport/input
modules are contracts only. The core mirrors native transitions and exposes a
read-only StateFlow; orchestration must serialize mutation. UI displays an empty
device list. Backup/device-transfer exclusions and cleartext denial are configured.
No emulator/device UI run has been verified.

### Capture Boundary

Windows capture uses a high-performance hardware D3D11 adapter and a two-buffer
free-threaded WGC pool. The owner polls and lends a GPU texture synchronously;
no texture reference or unfinished GPU use may outlive that consumer call. It
drains at most two frames per poll, discards invalid dimensions, and recreates the
pool on resize. The only OS event handler weakly signals target closure; owner
methods do cleanup. Permission/closure/device-loss failures propagate as typed
errors and numeric HRESULTs. There is no automatic restart, preview, CPU readback,
audio, or encoder yet. Read capture architecture docs before integrating NVENC.

Linux policy tests, formatting, and GCC analysis pass. The missing C++/WinRT
Foundation header from the initial Windows build has been corrected. Native
Linux/Windows and native-quality CI jobs pass for capture revision
`e3d8fe73a6c765db4c35f7d03fadf44680325405` in
[this run](https://github.com/dahalujwal1000/CloudPlay/actions/runs/37000958387).
Windows CTest reports six passed tests, including `capture.validation` and
`capture.invalid_cli`; Linux has four passed tests.
Live texture/resize/minimize/device-loss testing has not run. The probe accepts an
explicit HWND and duration; see the Windows checklist for the user's next action.

## Next Work

1. Complete Linux capture acceptance: retest continuous motion at 1920x1080/60 Hz,
   leave sharing active for >=30 seconds, investigate cadence/import timing if
   throughput remains below target. User agreed to switch temporarily to 60 Hz
   and provide a ready message. Do not change display settings automatically.
   Windows capture and its historical CI results remain valid but live acceptance
   is unverified; Windows tests are no longer the primary development gate.
2. Verify captured-frame NVENC interoperability and required CUDA/OpenGL development
   prerequisites before TASK-003 production encoding. Keep media GPU-resident.
   Do not treat EGL images as NVENC input handles or FFmpeg tests as an encoder.
   TASK-004 WebRTC is explicitly gated until stable 1080p60 GPU capture; do not start it.
3. Add actual cleanup and metrics as resources are introduced. State reducers alone
   do not release input, capture, encoder, transport, or game resources.
4. Define payload schemas and per-session authorization before implementing the
   planned signaling messages. Bootstrap authentication is not device pairing.

Pairing, credential storage, game launch, input transport, WebRTC, and NVENC
are all future work. State names and failure codes do not imply those features work.
See `docs/protocols/session-protocol.md` and `docs/protocols/signaling.md` for the
implemented foundation contracts and their remaining limitations.

## User Hardware and Open Details

- Current host: Fedora Linux 44 Workstation, verified from `/etc/os-release`.
- GPU: NVIDIA GeForce RTX 3050 6GB Laptop GPU, driver/KMD 615.71.09,
  6144 MiB VRAM and CUDA UMD compatibility 13.4 verified with unsandboxed nvidia-smi.
- Previously reported CPU/RAM: Intel Core i5-13420H, 16 GB RAM. Prior Windows 11
  Build Tools confirmation does not establish any Linux toolkit prerequisites.
- Android: Android 16.
- Still unconfirmed: Android model and CUDA Toolkit choice. Full SDK with samples
  is not installed; the user supplied the interface-header archive instead.
- Local tools: CMake, Ninja, GCC 16, Node 22.23.1, npm. Isolated Java 17, Gradle 8.13,
  Android SDK platform 36/build-tools 35.0.0, and native format/analyzer tools were
  downloaded under `/tmp` for verification. The default system Java is 25; use 17.
- Temporary verification paths: `/tmp/cloudplay-jdk17`, `/tmp/cloudplay-android-sdk`,
  `/tmp/cloudplay-gradle`, `/tmp/cloudplay-tools`. They are not repository dependencies.

NVIDIA's official interface 13.1.15 archive is extracted under ignored
`.cache/nvidia/Video_Codec_Interface_13.1.15/Interface`. This is headers only, not
the full SDK. No CUDA Toolkit/nvcc, Windows SDK or WebRTC SDK is installed here.
The supplied `/home/ujwal/Documents/Video_Codec_Interface_13.1.15.zip` exists and
matches the official interface archive SHA-256 documented in the setup guide.
It is not the full SDK with samples. Full SDK download requires the user's
login/license acceptance if samples are needed. Do not request account credentials.

Linux NVIDIA runtime libraries are present. Sandboxed nvidia-smi cannot access
GPU device nodes; unsandboxed queries and FFmpeg checks succeeded. Do not
reinstall the working driver based on the sandbox failure.
H.264 NVENC encoded 120 synthetic 1080p60 frames at a 12 Mbps target, P1/ULL,
CBR, no B-frames/lookahead and one-frame VBV in about 0.34 seconds. HEVC encoded
60 frames successfully. AV1 encode was explicitly rejected as unsupported.
No private content was captured; encoded output was discarded. This is a short
capability check, not sustained performance or capture/streaming acceptance.
See `docs/testing/linux-nvenc.md` for reproducible commands and limitations.

### Linux Capture Boundary

The Linux capture option is `CLOUDPLAY_LINUX_CAPTURE=ON`; it requires PipeWire,
GIO Unix, EGL and libdrm headers installed by the user. Installed versions now
include PipeWire 1.6.9 and GIO 2.88.3. Windows capture files and NVENC diagnostic
tests were not modified. Build `build/capture-and-nvenc` enables both modules.

One owner thread polls a restricted portal-provided PipeWire remote. Private
GMainContext subscriptions are removed before callback storage dies. Startup
asks for one monitor/window with no persistent tokens or remote-input permission.
DMA-BUF modifiers are intersected with NVIDIA EGL/CUDA-device-0 support. All
delivered descriptors/images/fds are borrowed during the synchronous consumer;
GPU work must complete before returning. Images are destroyed before buffers
are returned; no CPU readback or unbounded queue exists. The diagnostic does no
GPU pixel reads, CUDA registration, conversion, encoding or WebRTC.

Observed: 1050 1920x1080 XRGB8888/linear DMA-BUFs imported in a completed 30-second
run, ~35 FPS, zero recorded local drops/sequence gaps and zero capture-module
copies. An animated attempt imported 412 frames at ~39-42 FPS before session
closure. Some unselected runs timed out at Start. The active monitor was 144 Hz.
Capture has NOT passed sustained 1080p60. Compositor/cross-device copy counts and
true capture-origin latency are unknown. Updated diagnostics report signed PTS
presentation age and future timestamps separately; do not claim PTS age is an
all-frame capture-origin latency measurement.

All 15 combined native tests passed outside the IPC-restricted sandbox, including
unchanged NVENC readiness tests, frame leases/gate cases, owner-thread/repeated
shutdown checks and a private mock D-Bus portal cancellation/cleanup test.
Native formatting and GCC `-fanalyzer` passed for the Linux backend. All five CI
jobs passed at `fb5bbbcbd5ed8dd6ff563ea2cfbbadaf4947e9a6` in
[Linux capture CI](https://github.com/dahalujwal1000/CloudPlay/actions/runs/37005794506),
including Windows preservation, Linux builds/tests and clang-tidy. These CI tests
do not establish live 1080p60 or NVENC interoperability. No WebRTC work started.
The live readiness probe reports SDK/driver API 13.1 and interface ready. Its
CTest cases exercise CLI handling and isolated fake-driver failure paths, without
requiring a GPU in CI. Default domain tests still build without NVIDIA headers.
Local verification: SDK-enabled build and all 11 CTest cases passed; default build
and four tests passed; clang-format 18 and GCC 16 `-fanalyzer` passed. Linux native
build/tests and native-quality (including clang-tidy) passed at revision
`b85e460250a03175a33989881a132c8849a075c2` in
[readiness CI](https://github.com/dahalujwal1000/CloudPlay/actions/runs/37002479151).
Other jobs in that run were still in progress when checked; do not infer a full
run success from those two verified jobs alone.

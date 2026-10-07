# Coding Agent Handoff

Last verified: 2026-10-07.

Latest capture work: added --diagnostic-max-fps 60..65, default 60, with range
negotiation only; nominal target remains 60. Non-default ceiling runs cannot
report production acceptance. Added CLI, SPA parameter and option validation
tests. Build and all 45 CTests pass, changed C++ passes clang-format 18 and
pipewire_capture/video_rate tests pass GCC analyzer syntax checks. Windows code
and NVENC tests were not edited; Windows was not rebuilt locally.
Live baseline: 38.1994 FPS. Ceiling 61 control: 40.7987 FPS, actual negotiated
maximum 60.0035. Both ran 30s, balanced all buffers, zero local drops/gaps/copies;
neither passes the gate. No PNG/video or fresh pixel comparison. Display mode
unchanged at 60.0035 Hz non-VRR. See linux-capture-ceiling-2026-10-07.json and
linux-capture.md. Next needs compositor timing/rate-limit tracing, not another
speculative pool-size change. No captured-frame NVENC/WebRTC integration.

Pairing lifecycle hardening: response fields require their expected JSON types
instead of getString/getLong/getInt coercion. A rejected health authorization
clears the credential, and subsequent checks fail locally. Health checks compare
the credential revision after the response and recheck expiry, preventing stale
success after forgetting/re-pairing or expiration. Real TLS fixture regressions
cover malformed types, rejection invalidation and disconnect during health.

Android now has a manual pairing form and pinned HTTPS client, replacing the
empty device-list shell. Users independently confirm the full certificate SHA-256
fingerprint on the PC before submitting a challenge UUID and one-use code.
Platform hostname verification is retained; wrong pins/hostnames fail before
HTTP delivery. Device credentials remain memory-only, with no administrator
token on the phone. Cancel/disconnect and disposal forget credentials; an
in-flight pairing response cannot restore a forgotten credential. Successful
pairing requires an authenticated health check. No game/media controls were added.

Android APK assembly, app/UI lint and Spotless checks pass. Networking JVM TLS
tests cover success, pin/SAN rejection, input validation, rate limits, redirects,
oversized responses, and in-flight forgetting. JDK 17 and SDK 36/build-tools 35
are available under ignored .cache/android-tools and .cache/android-sdk locally.
No physical Android device is connected; UI layout, platform TLS behavior and
phone-to-host enrollment remain unverified. See docs/testing/android-pairing.md
for installation/setup and limits. Next: verify on the phone, then add persistent
device credential/reconnect handling with secure storage and lifecycle tests.
Capture FPS acceptance remains blocked; no captured-frame NVENC/WebRTC integration.

Earlier implementation history follows; pending Android trust/UI references
below are superseded by the status above.

Identity provisioning CLI added: npm run identity:provision -- --host <IP>, and
identity:inspect -- --identity <name> --host <IP>. OpenSSL generates EC P-256,
90-day self-signed server identities using captured stdout for key+certificate;
Node crypto parses PEM into the existing TLS validation contract. No private key
file, secret CLI argument, or secret console output. Secret Service storage uses
stdin under a fresh host-UUID name, followed by validation/fingerprint read-back.
Only public identity/IP/fingerprint/expiry metadata is returned. No activation or
automatic overwrite/rotation. A partial store failure may leave an unused entry;
the CLI reports its public name for inspection and never deletes blindly.
All23 signaling tests plus build/lint/format pass, including real generation and
HTTPS verification against a fake keyring. Real keyring provisioning remains
unverified and no production identity was created. Android trust UI/enrollment,
renewal, persistent device credentials and PC UI are still pending. See the
updated docs/testing/signaling-tls.md for setup and limits. Media gate unchanged.

TLS follow-up: signaling supports optional HTTPS/WSS, retrieving the certificate
and key from Secret Service via bounded secret-tool lookup (5s/64KiB), with no
plaintext identity persistence. Startup verifies validity dates, key matching and
bind-IP SAN. Non-loopback binds require explicit CLOUDPLAY_ALLOW_LAN=true plus
CLOUDPLAY_TLS_IDENTITY and an RFC1918 IPv4 address; no wildcard/public binds.
Missing/invalid identity fails closed, never falling back to HTTP. Default remains
loopback HTTP. All18 signaling tests, build/lint/format pass, including real TLS
sockets with normal trust verification, wrong-host/untrusted-certificate failures.
Tests inject the keyring reader; live Secret Service provisioning/lookup and phone
connectivity are unverified. No credentials provisioned, firewall changes or LAN
listener started. Android UI/client trust, device credential storage and renewal
remain pending. See docs/testing/signaling-tls.md. Capture/media gate unchanged.

Pairing foundation: signaling now has a single-owner ephemeral PairingService and
POST /v1/pairing/{start,complete,revoke}. Start/revoke require administrator auth;
complete authenticates with a two-minute, one-use 8-digit code plus UUID challenge.
Five guesses invalidate the challenge; ten completions/minute process-wide plus
per-IP route limits. Rotation does not reset the global budget. Sixteen device
slots, 256-bit tokens, 15-minute lifetime, digests only; restart clears everything.
Device tokens authorize only health/status diagnostics, never pairing management
or host control. Revocation terminates existing device sockets, and messages
recheck validity. HTTP responses are no-store; request secrets remain unlogged.
No network binding expansion, persistent credentials, Android UI/client, host
security integration or media work. Next establish secure transport/trust and
OS credential storage before LAN/Android enrollment. Protocol details and limits
are in docs/protocols/signaling.md. Capture performance gate remains blocked.

Latest: completed controlled refresh experiments and fixed a negotiation bug.
At 119.877 Hz GPU capture was 30.7658 FPS; at 144.003 Hz, 48.0317 FPS. Exact
framerate 60/1 failed negotiation. At 59.9339 Hz the old exact maxFramerate 60/1
also failed. maxFramerate now offers a positive capped range, verified using SPA
filter/fixate tests: the same 59.9339 Hz source now negotiates and delivers
30.0331 FPS. Original 60.0035 Hz with the fix delivers 38.1656 FPS. None pass.
All completed runs returned every buffer without local drops or sequence gaps.
Original display mode restored; all source/probe processes stopped.
Acceptance now explicitly checks balanced buffer counts and finite timings.
Isolation runner adds GPU-only/fixed-rate controls; 18 Python tests pass.
Fresh build/capture-fps-fix compiled all enabled capture/input/games/NVENC targets;
45 CTests pass. Changed C++ passes GCC analyzer and clang-format18, Python Black.
No Windows build, new pixel validation, captured-frame NVENC or WebRTC integration.
Do not call negotiation repair a throughput fix. CPU direct-scanout stall remains.
Next obtain compositor scheduling traces or compare compositor versions under the
same source/display controls; avoid speculative pool changes. Details/evidence:
docs/testing/linux-capture-refresh-controls-2026-10-06.json and linux-capture.md.

Composition-control follow-up: the native motion source now supports optional
--composition-control, a synchronized 48x48 child surface, and reports zero-copy
presentation counts. The default source and production capture path are unchanged.
The paired 30-second monitor runs completed: GPU 38.266 FPS (1148 frames), CPU
41.2655 FPS (1238 frames). All frames returned, zero local discards/sequence gaps.
Source feedback had no zero-copy presentations in either run; source FPS across
probe spans including portal selection was 59.0952/59.9169. All six node snapshots
showed running sources. GPU fence/held p99 was 6.59625/6.89913 ms; CPU held p99
0.025859 ms. CPU delivery no longer stalled under this control, consistent with a
presentation-path dependency, not proof of an exact Mutter bug or a production fix.
Neither path passes the 60 FPS gate. No pool tuning, encoder or WebRTC integration.
Evidence: docs/testing/linux-capture-composition-controls-2026-10-05.json.
Next isolate refresh/rate scheduling with separate controlled runs. Keep the 60 Hz
baseline; changing to 144 Hz is an experiment, not an established fix.

Monitor stall follow-up: --snapshot-node now records at source discovery and10/20s,
bounded to3 attempts with individual status, actual timing and unique files. Poll
observers are skipped after probe exit. Source feedback now includes deduplicated
presentation flags/zero-copy counts. All16 isolation tests and Black checks pass.
At unchanged60.003536Hz non-VRR scale1, CPU monitor capture stalled after5frames
in30.001s; all returned. All3 snapshots showed the same running node/serial.
Source remained59.8961FPS; full-trace zero-copy count3166/3366 (not capture-aligned).
Mutter50 upstream code contains minimum timestamp interval throttling and a
DMA-BUF-only monitor pre-paint scanout path. These motivate investigation but are
not a proven root cause. Display settings were not changed. Keep60Hz baseline;
compare120/144Hz separately and investigate scanout-vs-composited CPU delivery.
Artifact: docs/testing/linux-capture-monitor-stall-2026-10-05.json. Source links and
measurement caveats are in docs/testing/wayland-motion-source.md. GPU60FPS gate,
captured-frame encoder and WebRTC remain pending.

Window follow-up: existing new --source-target/--snapshot-node runner changes were
preserved and repaired. Missing sys import plus broad exception swallowing hid
snapshot failures; the test pre-created the output without verifying subprocess
execution. Snapshots now report saved/failed/not-observed status, reject wrong-node
or malformed output and retain bounded child failure detail. Regression tests cover
launch/timeout/exit/invalid output and exactly-one launch. All16 isolation tests and
Black formatting pass. User-consented live window controls: GPU39.9988FPS,
CPU41.4321FPS, source presentation59.8919/59.4181FPS across probe spans. All1200/1243
received frames returned, zero local discard/gaps; both GNOME node snapshots saved.
CPU window delivery remained active throughout30seconds, unlike prior monitor
CPU stall. Both modes still fail60FPS gate. No capture tuning or encoder work.
Next inspect monitor node state during stall and compositor frame scheduling.
Evidence: docs/testing/linux-capture-window-controls-2026-10-05.json.

CI foundation hardening: Linux native and quality jobs now enable capture, input,
games and Wayland motion together, with Wayland/GLES development dependencies.
Windows config is separate. Quality analysis now uses CMAKE_CXX_CLANG_TIDY during
the build, so enabled sources receive actual compiler arguments and protocol
headers are generated first. The filesystem-wide clang-tidy sweep was removed.
Clean local build/ci-foundation-check compiled every optional backend with LLVM22
clang-tidy, zero warnings/errors, and all46 CTests passed in5.02seconds. Exact
clang-format18 repository check and workflow YAML parsing passed. Development and
analysis RPMs were extracted under /tmp, not installed system-wide; local analyzer
needed its installed resource-directory override because its binary was extracted.
Hosted Ubuntu clang-tidy18 and Windows execution of this updated workflow remain
pending; do not claim a fresh GitHub CI pass. Documentation: docs/BUILDING.md.
Capture/NVENC implementation and tests unchanged. Next resume presentation-verified
monitor/window delivery isolation and investigate the CPU screencast stall.

Priority capture investigation resumed: optional CLOUDPLAY_WAYLAND_MOTION adds a
standalone native Wayland/EGL source with compositor presentation feedback,
fullscreen 1920x1080 bars/moving stripe/binary frame IDs, bounded feedback slots
and owner-thread cleanup. Generated official protocol bindings; no capture backend,
NVENC test, encoder or WebRTC changes in this milestone. Source-only NVIDIA run
presented1792 frames at59.8024FPS with zero discarded/invalid/pending feedback.
User-approved dual capture controls now have verified source presentation near
59.9FPS: GPU capture36.7664FPS; CPU capture stalled after3frames in30seconds.
GPU fence/import/held p99:5.38975/0.314853/5.63005ms; all buffers returned.
No pool/rate tuning justified or applied. Exact compositor/source-node cause still
unknown; stable1080p60 remains blocked. Next: compare monitor/window delivery with
live PipeWire node snapshots and presentation feedback, investigate CPU stall.
14 motion-configuration CTests pass; 45 combined tests pass across the sandbox run
and the two private-D-Bus reruns outside its socket restriction. New code passes
clang-format18 and GCC-fanalyzer. Source normal/SIGTERM cleanup live-tested; fake
compositor fault injection remains pending. All test processes stopped.
See docs/testing/wayland-motion-source.md and the dated presentation-controls JSON.
Older notes below describing FPS work as deferred are superseded by this entry.

Local check-ready game orchestration: CloudPlay::GameSession owns the immutable
catalog and injected process backend, accepts only loaded IDs, rejects unknown
IDs before backend launch and enforces single use. Explicit stop/detach only;
destruction never implies stop authorization. cloudplay_linux_game_runner is a
standalone local-consent diagnostic, not a remote authenticated endpoint. Default
is validation only; --launch is required. --seconds bounds monitoring; an active
child is detached with exit3 and a warning at the deadline. --stop-after explicitly
requests graceful SIGTERM; no implicit force escalation. Main host unchanged.
BUILD_TESTING generates game-manager-check.ini for the three-second harmless
fixture. Manual launch returned expected launch/exit JSON with exit0; timed-stop
CTest passes. 44 combined/12 default CTests, formatting and GCC-fanalyzer pass.
Commands and limitations: docs/testing/game-runner.md. No actual installed game,
capture permission, input event, production auth or WebRTC integration was added.
Next: user launcher configuration and compatibility checks, independently
authenticated main-host lifecycle wiring, descendant supervision and Windows.

Local profile catalog milestone: Linux game support now loads caller-selected
GLib key files into an immutable1..32-profile snapshot with exact-ID lookup.
64KiB/file limit; schema version1; strict required/allowed/duplicate key checks;
existing GameProfile bounds; duplicate ID rejection across files. No hand-rolled
parser or shell splitting. UTF-8/NUL checks precede parsing. The opened descriptor
must be regular, owned by effective UID and not group/other writable; final symlinks
are refused and FIFO open is nonblocking. Parent paths and same-user config remain
trusted. Catalog publication is all-or-nothing; failed reload leaves old snapshots
unchanged. Returned pointers/spans borrow catalog lifetime; serialize replacement.
`cloudplay_linux_profile_probe --file PATH` validates only and emits approved IDs,
never launches. examples/profiles/example.ini has nonexistent placeholders.
37 combined/12 default CTests, clang-format18 and GCC-fanalyzer pass. Tests cover
schema/escaping/snapshot/bounds and local-file trust. No real game or permission
dialog opened. See docs/testing/local-game-profiles.md.
Next game work: authorized approved-ID Host.App orchestration, actual launcher
configuration/compatibility, descendant supervision and Windows launching/loading.
No production auth/network control or captured-frame media integration was added.

Game process foundation: Host.Games now exposes bounded GameProfile validation
and IGameProcess. CLOUDPLAY_LINUX_GAMES=ON adds a GIO LinuxGameProcess backend for
single-use direct-child launch/poll, exit-code/signal reporting, explicit graceful
or force stop, and detach. GIO reaps children and provides race-free signaling;
no PID lookup, process-group kill, automatic escalation or game modifications.
Destruction relinquishes monitoring without killing the child. Profiles must be
trusted local configuration, never remote arbitrary commands; GIO's ENOEXEC shell
fallback means this is not a sandbox. Metadata/argument bounds and literal argv
handling are tested. No child stdout/stderr or arguments are logged.
All34 combined and12 default CTests pass, including repository-owned short-lived
fixtures for cwd/literal argv, launch failure, normal/nonzero exit, SIGTERM,
explicit SIGKILL and detach/destruction. clang-format18 and GCC-fanalyzer pass.
No actual game or launcher compatibility run occurred. Windows launching,
profile persistence, authorized Host.App orchestration and descendant tracking
remain pending. GLib2.82+ subprocess creation ignores SIGPIPE process-wide;
future networking must handle EPIPE. See docs/testing/linux-games.md.
Capture FPS remains deferred; encoder/WebRTC gates are unchanged.

Native input milestone: optional CLOUDPLAY_LINUX_INPUT=ON adds PortalInputSink
behind IInputSink using a dedicated RemoteDesktop portal session. Requests only
keyboard/pointer, validates grants, uses physical HID-to-evdev mappings and
acknowledged Notify calls. Private bus/context ownership; bounded request waits;
partial startup/session cleanup; single-use consent lifecycle; revocation refuses
further input. No persisted tokens, clipboard, screen capture or root access.
Aliases/duplicate edges are handled, invalid typed payloads are rejected, and
partially failed presses remain tracked for Dispatcher cleanup. Native permission
revocation requires teardown if releases are no longer authorized.
`cloudplay_linux_input_probe` defaults to consent-only/no input; --pointer-test is
explicit one-pixel motion only. It owns the native sink through InputSession.
No main host/network authorization or live transport wiring was added.
All32 combined,15 input-enabled and11 default CTests pass; clang-format18 and
GCC-fanalyzer pass. Fake portal tests are not live GNOME validation. Sanitizers
and Windows input validation remain pending. See docs/testing/linux-input.md.
Live user-approved GNOME consent-only check passed in5.00249seconds, grantmask3,
zero native calls/failures, zero dispatched events, exit0. Evidence:
docs/testing/linux-input-consent-2026-10-04.json. This does not verify event delivery.
Next gates: live revocation and native delivery checks, production EIS
transport/latency work, independent peer authorization, and actual host wiring.
Capture FPS tuning remains deferred; captured-frame encoding/WebRTC remain gated.

Host.App input ownership milestone: `CloudPlay::InputSession` owns the sink and
dispatcher in destruction-safe order and exposes receive/drain/close plus an
accepted-Core-transition hook. Stopping/Offline or GameStopped closes input and
releases held controls; failed cleanup retains the sink for explicit retry.
It is owner-thread only, noncopyable/nonmovable, and cannot reopen an old session.
Construct only after independent authorization. Fake-sink tests cover Core
disconnect/stop/failure/game-stop, pending discard, retry, destruction, session
binding and invalid construction. No native sink or host executable/transport
wiring was added; no input is emitted and authorization is not implemented.
Verification: 28 combined and11 default CTests, clang-format18 and GCC-fanalyzer
build pass. No live capture, Windows/MSVC or sanitizer run in this milestone.
Next input work: choose a permission-backed Fedora/Wayland native adapter,
validate its mapping and teardown, then connect authorized host transitions.
Capture FPS tuning and WebRTC acceptance gate remain unchanged.

Input dispatch foundation: `Dispatcher` adds a fixed-capacity FIFO (1..64),
bounded owner-thread draining and held-key/button tracking behind `IInputSink`.
Overflow or sink failure stops admission, discards queued events and attempts all
held releases. Failed releases remain in `CleanupFailed` for explicit `close()`
retry. Presses are tracked before sink application to cover partial failure;
release operations must be idempotent. Sink lifetime must exceed dispatcher
lifetime; destructor cleanup is best effort, not an OS release guarantee.
Fake-sink tests cover FIFO/budgets, wrap, full capacity, overflow, partial presses,
mixed release success/failure, retry, reentry and destructor cleanup.
No native input injection, transport, host disconnect wiring or WebRTC was added.
Capture FPS tuning and deferred live lifecycle checks remain pending.
Verification for this milestone: all27 combined CTests and10 default CTests pass;
clang-format18 and the GCC-fanalyzer build pass. Existing NVENC tests and capture
backends were not modified in this milestone. Windows/MSVC and sanitizers were
not run; the sanitizer runtime limitation described below remains unresolved.

Live capture checks/FPS tuning remain deferred at user request. Independent
Host.Input foundation added as `CloudPlay::Input`: documented v1 CPIN big-endian
32-byte header,34/36-byte keyboard/mouse packets, allocation-free bounded decoder,
typed payloads and per-session monotonically increasing uint64 replay watermark.
Rejects malformed length/magic/version/type/session/sequence/timestamp/payload;
invalid packets never advance replay state. Keyboard uses an initial HID page07
subset (power excluded); signed mouse deltas limited to4096 per axis. Session ID
is NOT authentication, timestamps are NOT synchronized/freshness evidence.
The initial decoder milestone added no network endpoint, transport or input injection;
the later dispatcher milestone above adds queue and held-key state only.
Future transport must preserve edges/relative deltas (not latest-state dropping),
with host disconnect release wiring. Touch/controller, platform mapping
and Android sender pending. Golden wire vector, bounds/truncation/replay checks,
single-bit mutations and10,000 malformed cases pass. Builds:26 combined CTests,
nine default CTests, clang-format18/GCC-fanalyzer pass; Windows/CI not run.
ASan/UBSan could not link because libasan.so.8.0.0 and libubsan.so.1.0.0 are
missing despite compiler stubs; sanitizer runtime verification was not performed.
Protocol: `docs/protocols/input-protocol.md`. Capture/NVENC backends untouched.

Explicit restart-check implementation: `--capture-seconds 3 --capture-runs 2`
reuses one CaptureSession/backend for up to3 consented runs, default1. Any failure,
denial, cancellation, cleanup failure or buffer-return mismatch stops subsequent
runs; this is not automatic error recovery. Per-run metadata and cumulative
start counters expose restart behavior. Mock/CLI tests pass for reuse, failure
suppression, bounds and invalid combinations. Live restart/revocation checks are
deferred by user. FPS/encoder/WebRTC gates unchanged.

Live host capture startup/shutdown verified: `--capture-seconds 5` exited0 after
5.00727s of frames,181 received/delivered/released/GPU imports, zero CPU frames,
discards/sequence gaps/failures/stop failures. All buffers returned; lifecycle
Starting -> Capturing -> Stopped and Ready -> Stopping -> Offline. This is NOT
1080p60 acceptance, pixel validation or NVENC interop. Sharing revocation and
live restart remain pending. Artifact:
`docs/testing/host-capture-lifecycle-2026-10-03.json`. No test processes remain.

Host capture wiring now implemented: `cloudplay_host --capture-seconds 1..120`
uses PipeWireCapture through CaptureSession only when Linux capture is built.
Default smoke/bitrate CLI remains unchanged. Timed duration begins at first frame;
startup polling timeout10s, SIGINT/SIGTERM request owner-thread cleanup (pending
portal consent cannot be interrupted and remains bounded by backend deadlines).
Numeric/typed JSON reports lifecycle, backend counters and buffer-return equality,
without arbitrary exception text, pixel reads or performance acceptance claims.
Missing-session failures emit CAPTURE_FAILED then OFFLINE; no automatic retry.
Tests cover CLI validation/unavailable backend/missing session, injected duration
and cancellation paths, denial, cleanup failure and buffer-return mismatch. Live
host consent/start/shutdown passed; revocation/restart remain pending.
Verification: 25 combined CTests, eight default CTests, formatting and
GCC-fanalyzer pass. Windows/CI not run; no FPS tuning/NVENC/WebRTC integration.

User deferred FPS tuning. New Host.App `CloudPlay::CaptureSession` library owns
an injected IFrameCapture with explicit owner-thread lifecycle and restart.
Supports async Starting/no-frame polling, borrowed callback/reentry guards,
failure cleanup after backend unwind, preserved primary/cleanup exceptions,
CleanupFailed restart blocking, explicit cleanup retry, idempotent stop and
best-effort destructor cleanup. Counters and backend metrics are exposed; no
automatic portal retries, idle watchdog or frame retention. Host executable is
still the foundation smoke lifecycle by default; opt-in wiring is recorded above.
Windows backend/API and working NVENC tests are unchanged; Windows adapter/live
recovery remain pending. FPS gate is unchanged; no encoder/WebRTC integration.
See `docs/testing/capture-session.md`. Verification: 24 combined CTests, seven
default CTests, clang-format18 and GCC-fanalyzer; Windows/CI not run for this step.

Latest NVIDIA-source dual controls completed: GPU38.8321 FPS/30.0009s, CPU37.2332
FPS/30.0001s, minimum intervals36.9702/32.9726 FPS. All1165/1117 frames returned;
zero discards/sequence gaps/outstanding buffers/copies. Source PID14568 was
verified active NVIDIA graphics via pmon (SM48%, memory7% in two samples).
Both were 1080p BGRx stride7680; GPU linear DMA-BUF/EGL, CPU mapped MemFd.
GPU fence mean/p99 4.657/5.979ms, held p99 7.335ms; CPU held p99 0.027083ms.
Producer interval p50 was33.321/33.141ms. NVIDIA did not remove the capture
cadence limit. No pool8 experiment was justified or executed.
New `scripts/pipewire_node_snapshot.py --node NODE --output NEW.json` stores a
filtered private/exclusive snapshot with a five-second query timeout. It validates
node type, omits arbitrary properties and limits each selected parameter to32
entries with explicit truncation. Live CPU source node70 was running/driver,
SPA Format BGRx 1920x1080, framerate0/1, maxFramerate60/1. Latency advertised
zero fields and node.rate/node.latency were absent: actual clock quantum is NOT
verified. Source presentation feedback remains absent; origin latency and
current pixel correctness are also unverified. Snapshotting can perturb timing.
Artifact: `docs/testing/linux-capture-nvidia-controls-2026-10-03.json`.
Next: independently measure source presentation/clock cadence, then compare
compositor output cadence. Preserve fences and acceptance gate. No test processes
remain running. Fourteen Python checks pass; no NVENC/WebRTC integration added.

NVIDIA source selection is implemented in `scripts/nvidia-motion-reference.sh`:
Wayland/OpenGL, PRIME offload, NVIDIA-only EGL vendor, conflicting DRI_PRIME
removed, no global settings changed. Invoke with `sh`; no executable bit required.
Smoke-tested for 12 seconds: FFplay PID13658 appeared in NVIDIA pmon as G with
SM25/34%, memory3/4%; initialized OpenGL. FFplay stayed open after the 12-second
input ended and was stopped explicitly with SIGINT; no test remains running. The capture importer
already selects NVIDIA EGL/CUDA. Fresh matching NVIDIA-source capture controls
are recorded above.
Twelve Python checks now include launcher environment scoping, argument handling
and missing-vendor failure. Windows and NVENC tests remain unchanged.

Latest CPU-only retry completed 30.0001 seconds at 4.56665 FPS, with 137
received/delivered/released frames, zero discards/gaps/outstanding buffers and
minimum interval 0 FPS. User confirmed fullscreen motion stayed visible throughout.
SPA BGRx, mapped MemFd, one plane, stride7680, offset0, size8294400, full crop,
no EGL import/fence wait/pixel copies. Held p99 0.035486 ms; poll p99 1.3951 ms;
arrival max 12034.3 ms. Source stayed alive throughout capture but presentation
feedback is still absent. This does not support pool8 or bypassing GPU fences;
investigate source presentation and compositor/PipeWire node cadence next.
Runner now accepts `--control cpu` (default `both`) and preserves requested-rate,
negotiated-format/buffer events and all per-second intervals in the report.
Twelve Python tests pass. Artifact:
`docs/testing/linux-capture-cpu-control-2026-10-03.json`.
All launched processes have exited; encoder/WebRTC gate remains closed.

Fresh live isolation: monitor GPU control completed 30.0002 seconds at 38.8664
FPS (minimum interval 34.7966), with 1166 GPU imports/received/delivered/released,
zero local discards, sequence gaps, copies or outstanding buffers. Display was
1920x1080 at 60.0035 Hz non-VRR. SPA BGRx, linear DMA-BUF, stride 7680,
allocation 8294400, full crop, implicit fences ready, EGL imported. Fence
mean/p99 4.721/7.747 ms; held mean/p99 5.138/8.044 ms. GPU acceptance failed.
CPU control was interrupted at 10.2027 seconds after four returned frames:
FFplay exited following an Escape key event. The runner excluded it with code125
and `source_exited_or_replaced`, saved the partial summary and skipped pool
experiments. Source presentation remains unverified (no presentation feedback).
User initially deferred retry; the subsequent completed retry is recorded above.
Artifact: `docs/testing/linux-capture-isolation-2026-10-03.json`.

Isolation runner recovery now rejects source exit, zombies and PID reuse, polls
source identity every 100 ms, terminates/reaps the probe on failure or interruption,
and atomically saves each completed/failed run. Invalid baselines skip further
portal requests. Both controls require verified source presentation without
discarded feedback before a pool-size experiment. The example source no longer
expires during consent. Twelve Python tests cover evidence, eligibility, subprocess
cleanup, interrupted report persistence and CPU-only diagnostics. No encoder or
WebRTC integration was added.

CPU/GPU isolation controls are implemented: `--cpu-capture` runs a no-snapshot
mapped-memory timing control and cannot pass GPU acceptance. `--buffer-pool 2..8`
changes the preferred count while keeping range 2..8 and default 4. The probe
reports actual pool peak, dequeue batches, borrowed-buffer peak and return counts.
Buffer-pressure exception/shutdown tests pass. `scripts/capture_isolation.py`
records per-run source feedback/drop counters and open DRM device evidence,
preserves failed runs, and bounds each subprocess to 170 seconds. Open device
descriptors do not verify the active GPU; missing/malformed feedback does not
verify presentation. Pool experiments require completed CPU/GPU controls,
adequate source feedback and measured fence/hold tails, rather than FPS alone.
Twelve Python tests cover evidence parsing, recovery, source selection and eligibility. Fresh live controls
for this verified runner require user portal selections; earlier workspace
result artifacts have been retained. See the CPU/GPU isolation section in
`docs/testing/linux-capture.md` for commands and limitations.

Capture cadence instrumentation is implemented: optional `--timing-diagnostics`
reports bounded in-memory distributions for dequeue/PTS intervals, implicit fence
waits, import, callback, EGL cleanup, requeue, total selected-buffer hold and poll
intervals. Optional `--fixed-rate` requests exact 60/1 while preserving the default
range negotiation. Presentation age is now measured at dequeue before fences.
Tests inspect SPA rate PODs and timing percentile/overflow/reset behavior.
`--source monitor|window` now capability-checks and restricts portal selection,
records actual source type/node, and rejects mismatched metadata with cleanup.
Timing-mode failures preserve the first bounded, escaped PipeWire error plus
stream state, error object/sequence, source/rate, and lifecycle counters.
Sharing closure now reports `portal.session_closed`.
See `docs/testing/linux-capture.md` for commands and measurement limitations.
Live range comparisons completed at 60.0035 Hz non-VRR: monitor 39.5653 FPS
over 30.001 s, window 40.8900 FPS over 30.0074 s. Both negotiated 1080p BGRx,
linear DMA-BUF, stride 7680 with no local drops/gaps/readbacks/copies. Fixed-rate
monitor and window offers both failed before a frame with `no more input formats`;
keep range negotiation as the default. Monitor fence mean/p99: 4.617/5.259 ms;
window: 11.118/17.149 ms. The 1080p60 gate remains closed. One window run ended
after 643 frames and is excluded; a fresh run completed the full duration.
Machine-readable results: `docs/testing/linux-capture-cadence-2026-10-02.json`.
Next: independently verify FFplay presentation rate/renderer, then isolate
compositor/GPU synchronization and buffer backpressure. Do not remove fences,
duplicate frames, relax the gate, or add speculative PipeWire properties.
Local verification: all 26 combined CTest entries (including fourteen Python checks)
and nine default CTests pass;
clang-format 18, GCC `-fanalyzer`, and whitespace checks pass. The combined suite
requires local IPC access for its private portal-test D-Bus. Missing-session CLI
failure was checked with timing/fixed-rate/source flags enabled. Source capability,
mismatch cleanup and CLI option/default tests pass. Windows/CI verification for
these changes has not been run.

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
`--capture-diagnostics` now traces buffer lifecycle/layout/FDs and compares a bounded
PNG series. `--cpu-capture` negotiates mapped MemFd/MemPtr packed RGB without EGL.
Use the new immutable plain-bars reference to isolate intermittent artifacts;
do not treat this diagnostic work as encoder or performance acceptance.
CPU-only live retry returned all eight MemFd buffers; one PNG exactly matched the
plain-bars reference, while seven included desktop/dialog/foreground changes.
The full series failed (exit 3). Source-stable multi-frame acceptance remains
unverified; do not skip mismatches to pass. All 18 combined and five default tests,
formatting/GCC analysis and all five CI jobs pass for diagnostic-mode source.

Real desktop validation now has a `--desktop-validation new.png --seconds 30`
mode with capability-checked embedded cursor and two private PNG snapshots.
The live run delivered 921 DMA-BUF/EGL frames over 30.5203 seconds: 30.1766 FPS,
zero local discards, 17 producer sequence gaps and two diagnostic CPU readbacks.
SPA BGRx, 1920x1080, stride 7680, one linear DMA-BUF plane, zero offsets and
8294400-byte allocation were negotiated. Both PNGs look clean, including text,
borders and visible cursors; gradients and temporal motion remain unverified.
Files are `/tmp/cloudplay-real-desktop.png` and `.last.png`; they are not committed.
Do not count this readback run as zero-copy performance acceptance. Run a separate
30-second continuous-motion desktop probe without snapshots before attributing
its sequence gaps or advancing performance acceptance. All 19 combined and five
default CTests, clang-format 18 and GCC analysis pass for desktop-validation changes.
All five [CI jobs](https://github.com/dahalujwal1000/CloudPlay/actions/runs/37011712241)
passed for source revision 1766c74c915911b9ccf65dd996d2b3ca7b608306.
The user confirmed desktop motion, cursor and gradients looked correct (visual
acceptance, not independent exact-pixel ground truth). A second, no-readback run
delivered 974 GPU frames over 30.0001 seconds: 32.4666 FPS, minimum interval
19.9997 FPS, zero local discards/sequence gaps and zero reported CPU/GPU copies.
Mean producer interval was 30.8136 ms; mean EGL import 0.263963 ms. Low FPS
persists without readback. Investigate producer/compositor/source cadence without
duplicating frames or weakening the gate. Captured-buffer NVENC GPU interop is
the next validation, with development prerequisites still missing; integration
and WebRTC remain unstarted.

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
`CloudPlay::Capture`; enabling Linux capture adds `CloudPlay::LinuxCapture`.
Other host modules
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

1. Complete Linux capture acceptance: the uninterrupted 60 Hz retest delivered
   1163 GPU-backed frames over 30 seconds, averaging 38.7663 FPS, with no reported
   drops/copies. EGL import averaged 0.272952 ms; the gate still fails.
   Isolate source rendering/compositor pacing/cross-device handling. Real-desktop
   no-readback validation now reports 32.4666 FPS and mean producer intervals
   of 30.8136 ms with no reported losses. Do not weaken the gate,
   duplicate frames or change display settings automatically.
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
are returned; no automatic CPU readback or unbounded queue exists. Opt-in diagnostic
snapshots now map linear packed-RGB DMA-BUFs with fence/CPU synchronization and
save private PNGs. No GPU pixel reads, CUDA registration, encoding or WebRTC exists.

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
The final probe also reports negotiated/max FPS and mean/max EGL import time to
distinguish producer cadence from import cost. Local ASan/UBSan was attempted but
could not link: Fedora libasan/libubsan are absent. No sanitizer pass is claimed.
Final source verification: all 15 combined tests, all five default tests, formatting
and GCC analysis pass. All five CI jobs also pass at final source revision
`59a6b9705bc68076ca70e02987a1ef28acba7bec` in
[final capture CI](https://github.com/dahalujwal1000/CloudPlay/actions/runs/37006485726).
The 60 Hz retest completed at 38.7663 FPS (1163 frames/30 seconds); the gate failed.
A subsequent static-reference PNG capture matched all 2073600 RGB pixels exactly.
SPA BGRx (8), one linear DMA-BUF, 1920x1080, stride 7680, zero offsets,
8294400-byte allocation, full-frame crop, no transform/explicit-sync metadata.
Implicit POLLIN and CPU SYNC START/END completed. Pattern bars, diagonal lines,
and checkerboards are intentional in the immutable FFmpeg reference.
See `docs/testing/linux-capture.md` for snapshot commands, safety and limitations.
The new snapshot test raises combined CTest count to 16. Windows and existing
NVENC tests remain untouched. GPU sampling/NVENC interop and sustained 1080p60
are still unverified; do not infer acceptance from this single exact CPU snapshot.
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

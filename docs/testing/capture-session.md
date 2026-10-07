# Capture Session Lifecycle

`CloudPlay::CaptureSession` is an owner-thread Host.App orchestration library.
It owns an `IFrameCapture` backend and provides explicit start, poll, stop and
restart handling. It adds no capture, encoding or transport implementation and
is wired into an opt-in host diagnostic mode; the default executable still runs
its foundation smoke lifecycle. The Linux backend implements this interface. The retained Windows
backend still uses its existing API; a future adapter is required to use this
controller there. Neither backend was modified for this milestone.

## Ownership and State

Construct, use and destroy the controller on the backend's owner thread. All
calls must be serialized; there is no worker thread or internal synchronization.
The controller owns the backend uniquely and cannot be copied or moved. It
never stores a borrowed frame, native image, CPU pointer or DMA-BUF descriptor.
The synchronous consumer must finish GPU work before returning and must not
reenter or destroy the controller. Lifecycle reentry from a consumer is rejected.

`start(options)` is allowed from Idle, Stopped or Failed. It forwards options
without changing format, resolution, FPS, source selection or acceptance rules.
A successful backend start may remain Starting until owner-thread polling
finishes negotiation; both Starting and Capturing can be polled or stopped.
Each consumer callback temporarily enters Delivering. No-frame polls are normal:
static desktops may legitimately have no damage, so no idle watchdog is added.

Backend or consumer failure unwinds the backend poll before cleanup, allowing
borrowed resources to be returned before `stop()`. The thrown exception is
preserved in `last_error()` and rethrown to the caller. Successful cleanup enters
Failed; restart requires a new explicit `start()` call. There is no automatic
retry, backoff or portal reopening, especially after denial or sharing closure.

If cleanup throws, state becomes CleanupFailed. `cleanup_error()` preserves that
exception separately from the original failure. Restart is rejected until an
explicit `stop()` retry succeeds. Ordinary stop is idempotent after successful
cleanup. Destruction attempts best-effort cleanup without propagating exceptions;
the backend destructor remains responsible for native resource destruction.

## Host Diagnostic Mode

Build with `CLOUDPLAY_LINUX_CAPTURE=ON`, then run:

```sh
build/capture-and-nvenc/host/cloudplay_host --capture-seconds 5
```

Select a monitor in GNOME's sharing dialog. The host requests the existing
1920x1080/60 GPU capture options, polls through `CaptureSession`, and stops after
the requested 1..120 seconds measured from the first frame. No pixel readbacks,
PNGs, encoding, game launches or network traffic are added. This is a lifecycle
check, not the standalone capture probe's sustained 1080p60 acceptance test.
Unavailable builds and invalid/missing arguments exit2 before a sharing request.
The original `--bitrate-mbps` option and default smoke run remain available.

For an explicit restart check on the same controller and backend, use:

```sh
build/capture-and-nvenc/host/cloudplay_host --capture-seconds 3 --capture-runs 2
```

`--capture-runs 1..3` defaults to1 and is valid only after `--capture-seconds`.
Each successfully completed run stops capture before the next start, and each
start requires fresh portal consent. This is a bounded user-requested test, not
automatic failure recovery: denial, sharing revocation, backend/cleanup failure,
buffer-return mismatch or cancellation ends the entire command immediately.
Run records identify the sequence; session counters are cumulative, while backend
metrics follow the backend's per-start reset behavior.

JSON records contain capture states, received/released/delivered counts, import
counts, dimensions and failure/cleanup counters. Buffer-return mismatches fail
the run. `performanceAcceptanceEvaluated:false` and `nvencInteropVerified:false`
are always explicit. Arbitrary exception text is not logged. A capture failure
also emits the host session's typed CAPTURE_FAILED transition before cleanup
completes to OFFLINE. There are no automatic recovery/consent retries.

Ctrl+C or SIGTERM requests owner-thread cleanup and exit130 during polling.
Signals only set a signal-safe flag, never access capture resources. Consent
startup is synchronous: cancellation cannot interrupt a pending portal request;
it is checked after that request completes or its existing timeout expires.
Before any frame arrives, the runner has a ten-second polling startup deadline.

`host.cli` verifies argument bounds, help, unavailable backend and missing-session
failure cleanup. Injected-backend runner tests cover successful duration-based
shutdown, cancellation before start/during polling, permission denial, same-backend
restart and suppression of subsequent runs after failure. CLI tests cover run
count bounds, malformed options and rejection outside capture mode.
Actual desktop consent/start/stop and sharing-revocation checks remain separate
live tests; passing mock tests does not establish live recovery or pixel correctness.

October 3 live host check passed: five-second mode exited0 after5.00727s with
181 received/delivered/released GPU-imported frames at1920x1080, zero CPU frames,
local discards, sequence gaps, capture/stop failures or cleanup failures. State
advanced Starting -> Capturing -> Stopped, followed by host Ready -> Stopping ->
Offline. No images/video were saved, and no performance acceptance was evaluated.
Sharing-revocation and live restart remain untested. Machine-readable lifecycle:
[`host-capture-lifecycle-2026-10-03.json`](host-capture-lifecycle-2026-10-03.json).

## Diagnostics and Verification

`diagnostics()` reports cumulative start attempts, capture/start/poll failures,
stop failures and successfully completed consumer callbacks. `capture_metrics()`
returns the backend's own metrics by value; it does not retain frame resources.
Errors are not automatically logged, and the controller reads no credentials or
network data. A new start attempt resets stored exception pointers, not counters.

`host.capture_session` tests an injected backend without a desktop or GPU:
pending negotiation, no-frame polling, consumer reentry guards, exact frame
release before failure cleanup, permission denial, explicit restart, backend
disconnect, unexpected inactive backend state, cleanup retry, idempotent stop,
metrics access and destructor cleanup including cleanup exceptions.

Run with `ctest --preset dev` or the combined Linux capture/NVENC build. The
native code is platform-neutral, but Windows/MSVC CI for this addition has not
yet been run. This milestone does not establish live portal recovery or 1080p60.

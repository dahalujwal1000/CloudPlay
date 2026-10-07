# Fedora Wayland Input Validation

## Scope

The optional `CloudPlay::PortalInput` adapter implements the existing `IInputSink`
using the [RemoteDesktop portal API](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.RemoteDesktop.html).
It requests keyboard and pointer access only: CreateSession, SelectDevices(types3),
Start, then validates the granted device mask before allowing any Notify calls.
There is no screen capture, clipboard, touchscreen, persistence, restore token,
root access or network listener. Windows capture and existing NVENC tests are
unchanged. Portal consent is local permission, not remote-peer authentication.

The initial adapter uses acknowledged D-Bus Notify methods with a250ms timeout
per event. This is a correctness baseline, not a measured gaming-latency solution.
The portal recommends EIS/libei for production input. Do not mix Notify calls with
an established EIS connection. No EIS connection is opened here.

## Build And Automated Checks

Requires `glib2-devel`, C++20 and CMake on Linux. Default builds leave this backend
disabled and do not require GIO.

```sh
cmake -S . -B build/linux-input -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DCLOUDPLAY_LINUX_INPUT=ON
cmake --build build/linux-input
ctest --test-dir build/linux-input --output-on-failure
```

`input.portal` starts a private test bus and fake portal. No actual desktop input
or permission dialog is produced. Checks include cancellation at all three stages,
unavailable devices, partial grants, bounded Start timeout, no injection before
consent, key/button/motion signatures, invalid payload rejection, physical-key
alias handling, failed-call cleanup, revocation, repeated close and single use.
It also exercises the real adapter behind Host.App InputSession: a failed release
leaves CleanupFailed, then succeeds on explicit cleanup retry. Tests assert that
no persistent permissions are requested. CLI tests cover help, invalid duration
and absent desktop/session access.

Physical HID page07 usages map to Linux evdev codes, with no XKB +8 offset or
layout-dependent keysym conversion. Mapping facts were checked against the
[Linux HID mapping](https://github.com/torvalds/linux/blob/master/drivers/hid/hid-input.c)
and installed `linux/input-event-codes.h`. The two supported backslash usages
alias the same evdev code; releasing one does not release the other held usage.
Duplicate press/release edges are suppressed. A failed press is conservatively
tracked so Dispatcher can attempt a release even after a partially applied call.

## Live Consent/Lifecycle Check

Run only from the user's Fedora GNOME Wayland session when ready for the dialog:

```sh
build/linux-input/host/cloudplay_linux_input_probe --seconds 5
```

This requests keyboard/pointer permission but emits no events by default. Accept
the GNOME dialog, leave the permission active, and wait for the JSON result. Check
`ok:true`, granted_devices containing bits1and2, native_calls0, native_failures0
and dispatched0. Repeat with cancellation and revocation; both must fail cleanly.
Do not run the pointer test while a game or sensitive application is focused.

```sh
build/linux-input/host/cloudplay_linux_input_probe --seconds 6 --pointer-test
```

The explicit pointer test sends one relative delta per second, alternating +1/+1
and -1/-1; it never sends keyboard events or button presses. A complete even-length
run has zero net requested displacement; actual desktop cursor motion depends on
pointer acceleration and boundaries. JSON reports dispatched events and native
failures, not end-to-end input latency. Failure JSON uses the `PortalInputFailure`
enum numeric value from `portal_input.hpp`; arbitrary D-Bus error strings and
session handles are not logged.

## Lifetime And Failure Policy

One owner thread serializes start/pump/apply/close. There are no background callbacks
on another thread. Start uses a private GLib context and private bus connection;
response subscriptions are removed before their stack storage expires. Request
waits are bounded, and abandoned requests are closed. Failed startup tears down
any partial session and connection. Start is single-use; no automatic consent retry.

Host.App must pump even when no input is queued, inspect native state, and close
input admission when permission is revoked or the bus disappears. `apply()` also
pumps before every event. It refuses events outside Active. Failed Notify calls
return false; Dispatcher closes admission and attempts held releases. Revoked
permission cannot authorize a release call: such cleanup fails visibly and must
end by native session/resource teardown, not by claiming successful delivery.

Close unsubscribes callbacks, attempts Session.Close once, and closes the private
connection even if the portal fails. Repeated close is idempotent. Destroy the
dispatcher before the sink; InputSession enforces this ownership order. Teardown
or acknowledgments are not proof that the intended game received input.

## Verified And Pending

2026-10-04: 15 input-enabled CTests and32 combined capture/NVENC/input CTests pass;
11 default CTests, clang-format18 and GCC-fanalyzer builds pass. Automated tests use
a fake portal, not Mutter. A user-approved live GNOME consent-only run passed:
5.00249seconds, granted_devices3, zero native calls/failures and zero dispatched
events, exit0. Evidence: [live result](linux-input-consent-2026-10-04.json).
Live event delivery and revocation, layout/game compatibility,
Windows input adapter, Android interoperability, EIS throughput or native latency
benchmarks have not passed yet. ASan/UBSan runtimes remain unavailable locally.

Stable GPU 1080p60, captured-frame NVENC interoperability and WebRTC integration
remain separate pending gates. This milestone does not change them.

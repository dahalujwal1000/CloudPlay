# Input Protocol

Binary protocol is preferred for high-frequency input.

Logical packet:

```text
version
sequence
timestamp
eventType
deviceId/sessionId
payload
```

Events:
- TOUCH_DOWN
- TOUCH_MOVE
- TOUCH_UP
- BUTTON_DOWN
- BUTTON_UP
- AXIS_UPDATE
- GAMEPAD_BUTTON
- GAMEPAD_AXIS
- MOUSE_MOVE
- MOUSE_BUTTON
- KEY_DOWN
- KEY_UP

## Version 1 Keyboard/Mouse Foundation

This initial subset supports keyboard and mouse packets only. Touch, controller,
axis, scroll and abstract game-button events remain unassigned and unsupported;
the logical event list above is the intended roadmap, not a wire enum.

One message contains exactly one packet, with a32-byte header and2 or4 payload
bytes. Integers use network byte order (big endian); there is no padding:

| Offset | Bytes | Field |
| --- | --- | --- |
| 0 | 4 | ASCII CPIN magic |
| 4 | 1 | Version, exactly1 |
| 5 | 1 | Event type |
| 6 | 2 | Exact payload byte count |
| 8 | 8 | Unsigned sequence, nonzero, strictly increasing within session |
| 16 | 8 | Sender monotonic timestamp in microseconds, nonzero |
| 24 | 8 | Nonzero opaque session ID, bound by authenticated orchestration |

| Type | Event | Payload |
| --- | --- | --- |
| 1 | KEY_DOWN | uint16 keyboard usage |
| 2 | KEY_UP | uint16 keyboard usage |
| 3 | MOUSE_MOVE | int16 dx, int16 dy, each -4096..4096 |
| 4 | MOUSE_BUTTON | uint8 button1..5, uint8 pressed0 or1 |

Keyboard identifiers are physical usages on USB HID Keyboard/Keypad page0x07,
not Unicode characters or platform scan codes. Initial supported ranges are
0x04..0x65, 0x67..0x73 and0xE0..0xE7. Power usage0x66 is intentionally excluded;
other extended/consumer usages are unsupported. Identifiers follow the
[USB-IF HID usage tables](https://www.usb.org/sites/default/files/hut1_3_0.pdf).
Mouse button IDs mean left, right, middle, back, forward in that order.
Signed deltas use two's-complement encoding. No arbitrary strings are accepted.

The decoder rejects oversized/truncated/trailing data, bad magic/version/types,
wrong session, zero sequence/timestamp, unsupported keys, out-of-range deltas,
invalid buttons and noncanonical booleans. Parsing never allocates or reads
outside the packet. The session receiver rejects duplicate or older sequences;
invalid packets never advance its watermark. Sequence wrap is forbidden: renew
the authenticated session before exhausting uint64. Create a new receiver only
for a newly authorized session; do not reset replay state on the same session ID.

This library does not authenticate, rate-limit, inject OS input
or expose a network endpoint. Only authenticated orchestration may supply
its expected session ID; knowledge of that ID is not authorization. Sender clocks
are not compared to host clocks until a synchronization policy exists. Initial
transport integration must use one ordered reliable input channel. Future
unordered latest-state lanes need distinct replay/sequence policies. Relative
mouse deltas and key/button edges must not be latest-state coalesced.

### Bounded Dispatch Lifecycle

`Dispatcher` owns a fixed FIFO with configurable capacity1..64 (default64).
It preserves all relative deltas and key/button edges. `drain()` processes at most
eight events by default. All operations must run on one owner thread; there is no
background worker or concurrent producer support. The caller supplies an
authenticated session binding and an `IInputSink` that outlives the dispatcher.
An optional Linux portal sink is available; live transport wiring is not implemented.

Valid queue overflow or sink failure closes input admission, discards pending
events and attempts releases for every held key/button outside the queue. A
failed press may have been partially applied, so it is recorded before calling
the sink. Successful releases clear held state; failed releases remain in
`CleanupFailed` for an explicit `close()` retry. One failed release does not
prevent attempts for the remaining controls. Closed sessions cannot reopen.
Reentrant enqueue returns `Busy`; reentrant drain/close do no work.

The sink must not throw, destroy or reenter its dispatcher, and release operations
must be idempotent. Destruction performs best-effort cleanup only: persistent
sink failure cannot guarantee OS release. Future orchestration must explicitly
retry cleanup or tear down the native input resource. Counters track admission,
dispatch, malformed packets, overflow, discarded events and release failures
without logging packet contents. Fixed queue memory does not replace future
authenticated transport rate limits.

### Host Ownership

`CloudPlay::InputSession` in Host.App owns both the sink and dispatcher. Member
destruction order keeps the sink alive for dispatcher cleanup. Construct it only
after independent peer authorization; the constructor does not authenticate.
One instance serves one authorized session and cannot reopen after closure.

Orchestration must forward accepted Core transitions to `on_transition()`:
entering Stopping or Offline, or a GameStopped event, closes admission, discards
pending events and releases held controls. This covers disconnect, host stop,
failure and game termination. If cleanup fails, keep the session/sink alive and
retry `close()`; do not declare resource cleanup complete or resume admission.
Reconnect requires newly authorized session binding and a new instance, not a
reset of the old replay watermark. Destruction remains best effort.

The lifecycle hook is tested with Core transitions and fake sinks, but is not
connected to the main host executable or a network callback yet. The current smoke
host has no authorized peer and does not inject input. A standalone Linux input
diagnostic owns a permission-backed PortalInputSink through InputSession, without
a network peer. See [native input checks](../testing/linux-input.md).

Golden KEY_DOWN(W) vector, sequence/timestamp/session all1 (34 bytes):

```text
4350494e01010002000000000000000100000000000000010000000000000001001a
```

Host verification: `input.packet` runs fixed-vector, payload boundary, truncation,
header validation, replay/watermark and deterministic malformed-input tests.
`input.dispatch` uses fake sinks to cover FIFO/budget behavior, ring wrap,
capacity limits, overflow, partial application, cleanup retries, reentry and
destruction; no OS input is emitted.
`host.input_session` covers transition-driven closure, cleanup retry, pending
event discard, sink ownership/destruction, fresh session binding and invalid
construction. These are orchestration tests, not live input permission tests.
Default and combined builds plus GCC analysis pass. ASan/UBSan execution is not
verified because the local runtime libraries are missing. No Android/transport
interop or Windows/MSVC CI validation has been performed for this new module.

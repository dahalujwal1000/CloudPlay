# Linux Game Process Foundation

## Scope

Host.Games implements bounded `GameProfile` validation and `IGameProcess` with
an optional `LinuxGameProcess` backend. No game-specific logic, memory access,
injection, executable patching, credentials, redistribution or automation is
implemented. This treats a game as an ordinary user-installed process, consistent
with [ADR-005](../decisions/ADR-005-game-abstraction.md).

Profiles are trusted local code configuration. A future authenticated control API
may select an approved profile ID, but must not accept arbitrary executable paths,
arguments or working directories from a peer. Validation is not authorization,
a sandbox, executable provenance verification or protection against changes to
locally writable executables. No network control API or profile file parser is
included in the initial process milestone. The subsequent
[local profile catalog](local-game-profiles.md) adds trusted file loading and a
validation-only probe; it does not add a network control API.

## Profile Contract

Fields: stable ID, display name, absolute executable path, absolute working
directory and an argument vector. ID is1..64 ASCII letters/digits/hyphen/underscore;
display name is1..128bytes; paths are1..4096bytes, absolute on the host platform.
Control characters in these fields are rejected. Arguments allow empty strings
and spaces but reject embedded NUL, more than64 arguments, more than4096bytes per
argument or more than32768bytes including argument terminators.

Linux additionally requires a regular executable file with execute permission
and an existing working directory. Relative PATH search is not used. The working
directory is set for the child only. The host's environment is inherited; this is
not environment isolation. No per-profile environment overrides are implemented.

## Lifetime And Stop Semantics

Launch is synchronous until the OS spawn result; poll is nonblocking while the
direct child is active. States are Idle, Starting, Running, StopRequested, Exited,
Failed and Detached. Each backend instance is single-use. Ordinary nonzero exit
codes are reported as exit results, not mistaken for a spawn failure.

The backend uses the established [GIO Subprocess API](https://docs.gtk.org/gio/class.Subprocess.html)
for descriptor handling, child reaping and lifetime tracking. It does not register
callbacks referencing the C++ owner. GIO manages process exit independently of
the owner's lifetime. stdin is the null device; stdout/stderr are silenced, not
retained or logged. No process output, command line, environment or credentials
are added to telemetry. Diagnostics expose counters and typed failure reasons.

Arguments are passed as a vector, not concatenated into a shell command. Important:
GIO's underlying [GLib spawn implementation](https://github.com/GNOME/glib/blob/main/glib/gspawn-posix.c)
can fall back to shell interpretation on ENOEXEC. Profiles may also intentionally
name interpreters. Consequently, this is a trusted local launch facility, not a
guarantee that an arbitrary executable can never invoke a shell. OS executable
replacement races are not prevented by the preflight checks.

`request_stop(Graceful)` sends SIGTERM using GIO's
[race-free child signaling API](https://docs.gtk.org/gio/method.Subprocess.send_signal.html).
Force uses GIO force_exit and requires a separate explicit user request. A signal
request does not prove termination: continue polling until Exited. Duplicate stop
requests are idempotent; graceful never downgrades a force request. No automatic
deadline escalation or restart is performed.

Only the owned direct child is signaled. The backend never accepts an arbitrary
PID, sends a process-group kill, searches by process name or stops unrelated
applications. Launchers that spawn a separate game and exit are not yet tracked;
direct-child exit does not prove the game is gone. Proton/Wine/Steam launcher
selection and descendant supervision require separate supported lifecycle work.

Detach and destruction relinquish monitoring without sending a termination signal.
The game may continue running. Host.App must decide explicitly whether to ask the
user to stop or leave it running; destruction alone is not stop authorization.
GIO still reaps children. As documented by GIO, creating a subprocess on GLib2.82+
changes the host-wide SIGPIPE disposition to ignored. Future networking must
handle write failures explicitly instead of relying on SIGPIPE termination.

## Build And Tests

The pure profile model builds by default without GIO. Enable Linux process support
explicitly; it requires `glib2-devel`:

```sh
cmake -S . -B build/linux-games -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DCLOUDPLAY_LINUX_GAMES=ON
cmake --build build/linux-games
ctest --test-dir build/linux-games --output-on-failure
```

`games.profile` checks bounded metadata, absolute paths, argument count/size/NUL
and boundary acceptance. `games.linux_process` launches only the repository-owned
test fixture: literal spaces/metacharacters/empty arguments, child working
directory without changing host cwd, nonzero exit, missing/unexecutable files,
missing interpreter, graceful SIGTERM, ignored SIGTERM followed by explicit
SIGKILL, and continued operation after detach/destruction. Temporary marker files
are confined to a unique temporary directory and removed afterward. All fixture
modes are short-lived; no actual game is launched.

2026-10-04: 34 combined capture/NVENC/input/game CTests and12 default CTests pass;
clang-format18 and GCC-fanalyzer builds pass. Windows profile validation has not
been run on MSVC. Windows process launching, Host.App game orchestration, external
profile persistence/UI, actual game compatibility and sanitizer execution remain
pending. Capture/NVENC/WebRTC gates are unchanged.

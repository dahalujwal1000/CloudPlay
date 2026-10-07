# Local Game Manager Check

## Try The Built-In Fixture

The combined Linux build creates `game-manager-check.ini` pointing only to the
repository-owned harmless process fixture. It waits three seconds and exits0;
no actual game, capture session, input event or media stream is involved.

From the repository root:

```sh
build/capture-and-nvenc/host/cloudplay_linux_game_runner --file "$PWD/build/capture-and-nvenc/game-manager-check.ini" --game game-check --launch --seconds 5
```

Expected JSON lines:

```json
{"event":"launched","profile_id":"game-check"}
{"ok":true,"event":"exited","exit_code":0,"signal":null,"stop_requests":0}
```

To check an explicit graceful stop of that same fixture:

```sh
build/capture-and-nvenc/host/cloudplay_linux_game_runner --file "$PWD/build/capture-and-nvenc/game-manager-check.ini" --game game-check --launch --seconds 5 --stop-after 1
```

The result should report signal15 and one stop request. No automatic force stop
is available in this runner.

## Your Own Profile

The [example profile](../../examples/profiles/example.ini) has placeholders. Set its
executable, working directory and arguments to an ordinary locally installed
launcher, then validate without launching:

```sh
build/capture-and-nvenc/host/cloudplay_linux_game_runner --file "$PWD/examples/profiles/example.ini" --game example-game
```

Only adding `--launch` starts the selected program. `--seconds` bounds monitoring
to1..300 seconds, default30. It does not promise the game exits within that time.
If still active at the deadline, the runner detaches, exits3 and warns that the
child may remain running. `--stop-after` is explicit consent to send SIGTERM to
that direct child; it must precede the monitoring deadline. A child ignoring it
is detached at the deadline, not killed. Never use a stop flag on a launcher unless
you understand its supported shutdown behavior. Descendants are not tracked.

SIGINT/SIGTERM request interruption of monitoring once handlers are installed.
The runner does not forward them as force-stop requests. Terminal-generated
signals may independently reach the child through the OS process group.

Failure output uses typed catalog/process reasons or generic messages, not paths,
arguments, configuration contents, child output or exception strings. Selected
IDs are validated profile IDs, so JSON output does not interpolate arbitrary text.

## Orchestration And Authorization

`CloudPlay::GameSession` in Host.App owns a catalog snapshot and injected process
backend. Launch accepts an exact profile ID; an unknown ID does not call the
backend. The session is single-use after a backend launch. It forwards poll,
explicit stop and detach and exposes exit results/diagnostics. Destruction never
automatically requests game termination.

This local runner uses an explicit local CLI action as consent. It does not
implement remote authentication. The library must only be used by independently
authorized orchestration. It does not accept peer-supplied executable paths or
arguments, but a future caller must still authorize profile selection and ensure
local catalog provenance. Do not expose this command as a public control endpoint.
The main `cloudplay_host` smoke/capture path remains unchanged.

## Build And Verification

Linux game support builds the runner even when BUILD_TESTING=OFF. Only the built-in
test profile/fixture require BUILD_TESTING=ON.

```sh
cmake -S . -B build/capture-and-nvenc -DCLOUDPLAY_LINUX_GAMES=ON -DBUILD_TESTING=ON
cmake --build build/capture-and-nvenc
```

2026-10-04: 44 combined CTests and12 default CTests pass; formatting and GCC analysis
pass. Checks include injected-backend ownership, rejection of unknown IDs before
launch, single-use guards, explicit stop/detach, CLI validation-only mode, natural
fixture exit and timed graceful termination. A direct manual fixture run produced
the expected launch/exit JSON and exit0. This verifies the local profile-to-process
path, not actual Genshin/Proton compatibility, descendant lifetime, Windows game
launching, peer authorization, streaming or end-to-end gaming.

# Trusted Local Game Profiles

## Contract

`LocalProfileCatalog` is an immutable snapshot of1..32 explicitly selected local
profile files, available with `CLOUDPLAY_LINUX_GAMES=ON`. It uses the established
[GLib key-file parser](https://docs.gtk.org/glib/struct.KeyFile.html), not a custom
INI parser or command-line splitter. No new parser dependency is needed beyond
the existing Linux GIO/GLib dependency.

Each file has one logical `[profile]` group with exactly these keys:

```ini
[profile]
version=1
id=example-game
name=Example Game
executable=/absolute/path/to/user-installed/launcher
working_directory=/absolute/path/to/game
arguments=space arg;literal\;semicolon;
```

The example arguments are two separate strings: `space arg` and
`literal;semicolon`. This is GLib key-file list escaping, not shell quoting.
`arguments=` means no arguments. Empty argument elements are preserved. Unknown,
localized, missing and duplicate keys, unsupported versions, other groups,
invalid UTF-8 and embedded NUL are rejected. GLib merges repeated group headings;
the loader enforces one logical group, not one textual occurrence of its header.
Duplicate keys within merged groups are still rejected. Existing GameProfile
metadata/path/argument bounds apply after parsing.

Each file is at most64KiB, limiting a32-file load to at most2MiB of source text.
Catalog publication is all-or-nothing: malformed files or duplicate IDs reject
the whole new snapshot. Previously loaded catalogs remain unchanged. Lookup is
by exact case-sensitive ID, not path or command line. Returned pointers/spans are
borrowed from their catalog and must not outlive or survive replacement of it.
Callers serialize loading/replacement; no concurrent hot-reload API is provided.

## File Trust

Only absolute paths are accepted. The final component must not be a symlink.
The loader opens with no-follow, close-on-exec and nonblocking flags, checks the
opened descriptor, and reads from that same descriptor. It requires a regular
file owned by the effective host user with no group/other write permission. A
FIFO cannot block the loader. Reads remain bounded if a file grows after opening.

These checks are defense in depth, not remote-peer authentication or a sandbox.
Parent symlinks are not forbidden; parent directories and the same-user local
configuration are trusted. Concurrent same-user edits are not a transaction and
may cause rejection. Publish edits through a new file and atomic rename. The
loader never writes configuration or persists credentials.

Future host control must authorize a peer and accept only an approved profile ID
from it. Never forward peer-supplied paths, executable arguments or environment
variables to the loader or process backend. Do not expose the diagnostic as an
unauthenticated service. Do not store account secrets in argument vectors.

## Diagnostic

After building Linux game support:

```sh
build/capture-and-nvenc/host/games/cloudplay_linux_profile_probe --file /absolute/path/game.ini
```

Repeat `--file` for more profiles. The probe validates local trust, syntax and
profile bounds only, then prints validated IDs and `launched:false`. It never
launches a game, checks installed launcher compatibility or requests input/capture
permission. It does not log paths, names, arguments, contents or parser error
strings. Failure output reports a typed CatalogFailure numeric value.

The repository's [example profile](../../examples/profiles/example.ini) contains
nonexistent placeholders. It is a schema example, not an installed game setup.
Executable existence and execute permission remain launch-time checks. Users
must configure their own legally installed launcher separately.

## Verification

`games.local_profiles` covers GLib escaping and empty arguments, immutable snapshots,
unknown/localized/duplicate/missing keys, bad versions and IDs, malformed data,
invalid UTF-8/NUL, oversized and exact-limit files, symlink/directory/FIFO rejection,
group-write rejection, duplicate IDs and1..32 catalog bounds. Probe CLI checks
cover help and missing arguments. Tests use unique temporary directories.

2026-10-04: 37 combined CTests and12 default CTests pass; formatting and GCC analysis
pass. No actual game is launched in profile checks. Automatic profile discovery,
editing UI, Windows file-trust backend, production authorization and Host.App
production approved-ID game orchestration remain pending. A subsequent
[local game runner](game-runner.md) connects catalog lookup to Host.App GameSession
under explicit local consent. Capture/WebRTC gates are unchanged.

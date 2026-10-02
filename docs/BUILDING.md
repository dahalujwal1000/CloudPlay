# Building the Foundation

The default host is a lifecycle smoke test; Windows also builds a capture diagnostic.
Android is an unpaired client shell. Signaling implements authenticated diagnostics
only. Media transport, pairing, game launch, and input transport are not implemented.

## Native Host

Install CMake 3.25+, Ninja, and a C++20 compiler. On Windows, use Visual Studio 2022
Build Tools with Desktop development with C++, then open an x64 Native Tools prompt.
The existing foundation also builds on Linux for domain testing.

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

Run `build/native/host/cloudplay_host` on Linux or
`build\native\host\cloudplay_host.exe` on Windows. Optional configuration:

```sh
build/native/host/cloudplay_host --bitrate-mbps 12
```

Defaults: H.264 target, 1920x1080, 60 FPS, 12 Mbps. Configuration validates bitrate
from 6 through 20 Mbps. No hardware encoder is initialized. Valid execution emits
four JSON lifecycle records and exits. Invalid arguments exit with code 2.

On Windows, `build/native/host/capture/cloudplay_capture_probe.exe` acquires a window
as D3D11 textures. See [capture acceptance](testing/capture.md) for commands and the
manual checklist. Use a recent Windows SDK with C++/WinRT headers. The portable
frame-policy test runs on Linux; Windows adds capture validation tests.

With clang-format 18 and clang-tidy 18 installed, format/check the `.cpp` and `.hpp`
files in `host/`. CI checks formatting and analyzes translation units against
`build/native/compile_commands.json`. GCC users can additionally run:

```sh
cmake -S . -B build/analyze -G Ninja -DCMAKE_CXX_FLAGS=-fanalyzer
cmake --build build/analyze
```

## Android

Install JDK 17 and Android SDK platform 36 / build-tools 35.0.0. Set `JAVA_HOME`
to JDK 17 and `ANDROID_HOME` to the SDK, or configure the SDK in Android Studio.
Open `android/` as the Gradle project.

The project pins AGP 8.11.1, Gradle 8.13, Kotlin/Compose compiler 2.1.21, and a
Compose BOM. This matches the [AGP compatibility table](https://developer.android.com/build/releases/agp-8-11-0-release-notes).
The wrapper JAR and distribution checksum were verified against Gradle's published
checksums. Minimum Android version is API 26; target/compile SDK is API 36.

From `android/`:

```sh
./gradlew :core:test :app:assembleDebug :app:lintDebug :ui:lintDebug spotlessCheck
```

On Windows use `gradlew.bat`. `./gradlew spotlessApply` formats Kotlin and Gradle
scripts. Lint treats warnings as errors except dependency-update notices, because
versions are pinned and upgrades require deliberate compatibility review.

The APK is `android/app/build/outputs/apk/debug/app-debug.apk`. It displays an empty
host list; networking, streaming, and input modules contain contracts only.
No bearer token or connection to the diagnostics server is embedded in the APK.
The app disables backups and cleartext networking. A device/emulator UI run has
not yet been verified.

## Signaling

Use Node 22.13+ (or Node 24). From `signaling/`:

```sh
npm ci
npm run check
```

Generate an ephemeral bootstrap token into the process environment, without printing
or committing it. POSIX shell example:

```sh
export CLOUDPLAY_TOKEN="$(node -e 'process.stdout.write(require("node:crypto").randomBytes(32).toString("base64url"))')"
npm start
```

PowerShell:

```powershell
$env:CLOUDPLAY_TOKEN = node -e 'process.stdout.write(require("node:crypto").randomBytes(32).toString("base64url"))'
npm start
```

`npm start` requires `npm run build` first; `npm run check` already builds.
The server listens at `http://127.0.0.1:8787` by default. Supported environment:

| Variable | Accepted values |
| --- | --- |
| `CLOUDPLAY_TOKEN` | Required; 32-256 URL-safe token characters; generate randomly |
| `CLOUDPLAY_HOST` | `127.0.0.1` (default) or `::1` only |
| `CLOUDPLAY_PORT` | Integer 1024-65535; default 8787 |
| `CLOUDPLAY_LOG_LEVEL` | `silent`, `error`, `warn`, `info` (default), `debug` |

Both `GET /health` and WebSocket `/v1/signaling` require
`Authorization: Bearer <bootstrap token>`. Browser WebSocket APIs cannot set this
header; use an appropriate native/Node diagnostics client. There is no browser UI.
SIGINT/SIGTERM closes the listener and active sockets.

This token is a temporary local diagnostics credential, not device pairing or
persistent identity. Persistent credentials must use secure OS storage later.
The server deliberately refuses non-loopback binds; Internet transport requires
the security and connectivity tasks, TLS, and reviewed session authorization.

## CI and Status

`.github/workflows/foundation.yml` defines Linux/Windows native builds/tests,
native formatting/analysis, signaling checks, and Android build/test/lint/format
checks. Foundation CI has passed on GitHub, including Windows/MSVC. See
`docs/AGENT_HANDOFF.md` for revision-specific foundation and capture results.

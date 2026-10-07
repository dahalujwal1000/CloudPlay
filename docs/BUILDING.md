# Building the Foundation

The default host is a lifecycle smoke test; Windows also builds a capture diagnostic.
Android provides manual certificate-pinned pairing and authenticated health checks.
Signaling implements ephemeral pairing with optional Secret Service-backed TLS.
Media transport, persistent device credentials, and input transport are not implemented. Explicit
local game launch is available through the [game runner](testing/game-runner.md).

## Native Host

Linux/Fedora is now the primary host target; existing Windows capture is retained.
For NVIDIA interface-header setup, the optional readiness probe, and live hardware
checks, see [Linux NVIDIA setup](testing/linux-nvenc.md). The default build still
requires no NVIDIA SDK. The optional Linux backend and standalone diagnostic
build with `-DCLOUDPLAY_LINUX_CAPTURE=ON`; see [GNOME/Wayland capture](testing/linux-capture.md).
Stable 1080p60 capture and captured-frame NVENC interoperability remain pending.
The default executable is still a lifecycle smoke test, not a streaming host.

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
files in `host/`. Linux CI enables capture, input, games and the Wayland motion
diagnostic. CMake runs clang-tidy while compiling each enabled C++ target, using
its actual compiler arguments and generating protocol headers before analysis.
Generated protocol C bindings are compiled without the C++ analyzer. To reproduce
the full Linux quality configuration after installing the documented development
dependencies and obtaining the NVIDIA interface headers:

```sh
cmake --preset dev -DCLOUDPLAY_LINUX_CAPTURE=ON -DCLOUDPLAY_LINUX_INPUT=ON \
  -DCLOUDPLAY_LINUX_GAMES=ON -DCLOUDPLAY_WAYLAND_MOTION=ON \
  -DCLOUDPLAY_NVENC_INCLUDE_DIR=/absolute/path/to/Interface \
  -DCMAKE_CXX_CLANG_TIDY=clang-tidy-18
cmake --build --preset dev --parallel 2
ctest --preset dev
```

The input/capture lifecycle suites use private test D-Bus sessions; they do not
request GNOME screen-sharing or emit input. The motion CLI test uses an unavailable
display. CI cannot validate live GPU performance or portal consent/revocation.
GCC users can additionally run:

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
./gradlew :core:test :networking:test :app:assembleDebug :app:lintDebug :ui:lintDebug spotlessCheck
```

On Windows use `gradlew.bat`. `./gradlew spotlessApply` formats Kotlin and Gradle
scripts. Lint treats warnings as errors except dependency-update notices, because
versions are pinned and upgrades require deliberate compatibility review.

The APK is `android/app/build/outputs/apk/debug/app-debug.apk`. It displays a host
pairing form with independently confirmed certificate fingerprint, challenge ID,
and one-use code. See [Android pairing validation](testing/android-pairing.md).
No administrator bearer token is embedded or entered in the APK. Device tokens
are memory-only. The app disables backups and cleartext networking. A physical
device/emulator UI run and phone-to-host connection have not yet been verified.

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
| `CLOUDPLAY_HOST` | `127.0.0.1` (default), `::1`, or explicit RFC1918 IPv4 with LAN opt-in and TLS |
| `CLOUDPLAY_ALLOW_LAN` | `false` (default); `true` requires TLS for non-loopback binds |
| `CLOUDPLAY_TLS_IDENTITY` | Optional Secret Service identity name; enables HTTPS/WSS |
| `CLOUDPLAY_PORT` | Integer 1024-65535; default 8787 |
| `CLOUDPLAY_LOG_LEVEL` | `silent`, `error`, `warn`, `info` (default), `debug` |

Both `GET /health` and WebSocket `/v1/signaling` require
an administrator or short-lived paired-device bearer token. Browser WebSocket APIs cannot set this
header; use an appropriate native/Node diagnostics client. There is no browser UI.
SIGINT/SIGTERM closes the listener and active sockets.

The bootstrap token is an administrator credential and must not be shared with a
phone. The new [pairing API](protocols/signaling.md#local-ephemeral-pairing) issues
short-lived diagnostics-only device tokens. Test expiry, replay, scope, revocation
and secret-log exclusion with `npm run check`; tests never print issued secrets.
All device credentials are memory-only. Persistent credentials must use secure OS storage later.
The server refuses non-loopback HTTP, wildcard binds and public IP binds. See
[TLS configuration and trust requirements](testing/signaling-tls.md) before
provisioning an identity or enabling private LAN diagnostics. Internet transport
and reviewed session authorization remain pending.

## CI and Status

`.github/workflows/foundation.yml` defines Linux/Windows native builds/tests,
native formatting/analysis, signaling checks, and Android build/test/lint/format
checks. Linux builds include all implemented optional backends and the presentation
source; Windows retains its platform-specific configuration. Historical foundation
CI passed on GitHub, including Windows/MSVC; the expanded 2026-10-05 workflow still
needs a hosted run before claiming it passed there. See
`docs/AGENT_HANDOFF.md` for revision-specific foundation and capture results.

# Coding Agent Handoff

Last verified: 2026-10-02.

## Current Status

TASK-001 foundation scaffolding is implemented across the native host, Android, and
signaling, with CI definitions. Final Windows/MSVC and GitHub CI execution remain
unverified. Capture, encoding, streaming, and pairing have not been implemented.
Do not mark TASK-001 fully accepted until its verification gates pass.

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

The build exposes `CloudPlay::Core` and `CloudPlay::Telemetry`. Other host modules
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

All three tests passed: `host.core`, `host.smoke`, and `host.invalid_config`.
The last test expects a failing exit code for a zero bitrate. The unit executable
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
Windows/MSVC and GitHub workflows have not run from this workspace. A local Git
repository was initialized; this agent did not commit or push. Concurrently added
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

## Next Work

1. Run Windows/MSVC build/tests using `docs/BUILDING.md`. Execute the defined GitHub
   workflow on the user's repo; its remote results have not been inspected here.
2. Start TASK-002 after foundation verification: Windows Graphics Capture to D3D11
   textures. Verify official Windows APIs and define callback/resource ownership.
3. Continue TASK-003 NVENC, then TASK-004 PC-to-PC WebRTC. Keep media GPU-resident.
4. Add actual cleanup and metrics as resources are introduced. State reducers alone
   do not release input, capture, encoder, transport, or game resources.
5. Define payload schemas and per-session authorization before implementing the
   planned signaling messages. Bootstrap authentication is not device pairing.

Pairing, credential storage, game launch, input transport, WebRTC, NVENC, and capture
are all future work. State names and failure codes do not imply those features work.
See `docs/protocols/session-protocol.md` and `docs/protocols/signaling.md` for the
implemented foundation contracts and their remaining limitations.

## User Hardware and Open Details

- PC: Windows 11, Intel Core i5-13420H, NVIDIA RTX 3050, 16 GB RAM.
- Android: Android 16.
- Still unconfirmed: exact GPU VRAM/driver, Android model, and
  whether the user can execute builds on the Windows PC.
- Local tools: CMake, Ninja, GCC 16, Node 22.23.1, npm. Isolated Java 17, Gradle 8.13,
  Android SDK platform 36/build-tools 35.0.0, and native format/analyzer tools were
  downloaded under `/tmp` for verification. The default system Java is 25; use 17.
- Temporary verification paths: `/tmp/cloudplay-jdk17`, `/tmp/cloudplay-android-sdk`,
  `/tmp/cloudplay-gradle`, `/tmp/cloudplay-tools`. They are not repository dependencies.

Request missing device/build details when needed for platform testing. No Windows
SDK, NVIDIA SDK, or WebRTC SDK has been installed or integrated in this workspace.

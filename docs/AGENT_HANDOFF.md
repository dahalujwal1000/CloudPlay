# Coding Agent Handoff

Last verified: 2026-10-02.

## Current Status

TASK-001 Foundation is partially implemented. The portable C++ host foundation builds
and its current tests pass on Linux. Android, signaling, CI, capture, encoding, and
streaming have not been implemented. Do not mark TASK-001 complete yet.

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

The build exposes `CloudPlay::Core` and `CloudPlay::Telemetry`. Other host modules
remain planned; no placeholder implementation of hardware or external APIs exists.

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

Windows/MSVC builds have not run. Formatting/static analysis has not run;
clang-format was unavailable. No CI workflow exists. The current directory was
not a Git repository when checked during implementation; no commit was created.

## Next Work

1. Finish TASK-001 before proceeding to capture. Align the CMake preset schema
   version with the declared minimum CMake version, add Windows build instructions
   and CI, run formatting/static analysis, and expand validation tests as needed.
2. Create the Android Kotlin/Compose project with separated core, networking,
   streaming, input, and UI ownership; add a pinned Gradle wrapper and unit tests.
3. Create the TypeScript/Node/Fastify signaling foundation with strict configuration,
   schema validation, authentication, safe logging, tests, and a dependency lockfile.
   Define payload schemas before enabling signaling messages or control endpoints.
4. Document concrete cleanup ownership and lifecycle transitions in architecture
   and session protocol docs as implementation progresses. Add telemetry metric
   contracts without claiming measurements that are not collected.
5. After foundation verification, start TASK-002: Windows Graphics Capture to D3D11
   textures. Then follow TASK-003 NVENC and TASK-004 PC-to-PC WebRTC.

Pairing, credential storage, game launch, input transport, WebRTC, NVENC, and capture
are all future work. State names and failure codes do not imply those features work.
The attempted npm registry lookup eventually succeeded, but no Node dependencies
were installed and no signaling files were created.

## User Hardware and Open Details

- PC: Intel Core i5-13420H, NVIDIA RTX 3050, 16 GB RAM.
- Android: Android 16.
- Still unconfirmed: Windows version, exact GPU VRAM/driver, Android model, and
  whether the user can execute builds on the Windows PC.
- Local tools observed: CMake, Ninja, GCC, Node 22, npm, Java 25. No Gradle
  installation or Windows SDK was available. Establish a compatible Android JDK
  and SDK before claiming Android build verification.

Request missing device/build details when needed for platform testing. No product
decision is needed to continue the remaining foundation scaffolding.

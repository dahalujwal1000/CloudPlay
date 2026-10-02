# TASK-001 Foundation

## Goal
Provide independently buildable and testable host, Android, and signaling foundations.

## Scope
Build files, host Core/Telemetry/App, Android modules, signaling bootstrap, CI, and
associated tests/configuration/documentation.

## Non-goals
Capture, NVENC, WebRTC media, device pairing, live game integration, or input encoding.

## Requirements
- Use documented platform stacks and preserve subsystem ownership.
- Validate configuration and lifecycle transitions before side effects.
- Authenticate diagnostic endpoints and bound network input work.
- Avoid credentials in logs; keep bootstrap credentials ephemeral.
- Provide reproducible dependency versions and usable build instructions.

## Acceptance Criteria
- [x] C++20 CMake build, test targets, development presets.
- [x] Core session/configuration and structured lifecycle logging.
- [x] Linux native tests, format check, GCC static analysis pass locally.
- [x] Android module scaffold, verified wrapper/checksum, Kotlin unit tests.
- [x] Android debug APK builds locally.
- [x] Android app/UI lint and formatting pass after final changes.
- [x] Authenticated Fastify/WebSocket diagnostics, validation, config, safe logging.
- [x] Signaling tests, TypeScript checking, ESLint, Prettier pass locally.
- [x] CI workflow and documentation created; local Git repository initialized.
- [x] Windows/MSVC build and tests pass on hosted Windows CI.
- [x] GitHub CI workflow executes successfully.

Foundation verified at revision `bd302dd937476f38c2b84e5c64ad370dfc44a1f9`:
[all five jobs passed](https://github.com/dahalujwal1000/CloudPlay/actions/runs/36999404589).
Live GPU/device testing remains separate.

## Tests
- Native: CTest core lifecycle/recovery/config/logging, smoke, invalid config.
- Android: JUnit lifecycle/recovery and bitrate validation; APK build and lint.
- Signaling: Node tests for config/schema rejection, authentication, correlation,
  message/request bounds, secret handling, and socket cleanup.
- Hardware/performance and device UI tests belong to later stages.

## Security Considerations
The diagnostic service binds loopback only and authenticates upgrades before socket
acceptance. It has no game control routes. Pairing and persistent credential storage
are not implemented. Android has no embedded credential and disables cleartext/backup.

## Performance Considerations
No media copies or encoding exist yet. WebSocket payload and work budgets are bounded.
Do not interpret passing smoke tests as evidence of 1080p60 or low latency.

## Documentation
`docs/BUILDING.md`, `docs/AGENT_HANDOFF.md`, session/signaling protocol docs, README.

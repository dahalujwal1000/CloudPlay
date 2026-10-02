# Android Client Architecture

## Modules
- app: application lifecycle
- core: domain/state
- networking: signaling/session
- streaming: WebRTC/media
- input: touch/controller abstraction
- ui: Compose screens
- tests: unit/instrumentation

## Rendering
WebRTC video should be rendered to a hardware-backed surface where possible.

## Touch
Touch events must not directly construct network packets.

Touch
→ VirtualControl
→ InputState
→ InputManager
→ PacketEncoder
→ DataChannel

Controls must support:
- move
- resize
- opacity
- per-game layout
- reset
- hide/show

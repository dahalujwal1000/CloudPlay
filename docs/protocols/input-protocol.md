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

The exact binary layout must be versioned and documented before implementation.

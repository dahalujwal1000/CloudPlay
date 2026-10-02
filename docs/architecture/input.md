# Input Architecture

## Flow
Android touch/controller
→ Input abstraction
→ input state
→ binary protocol
→ WebRTC DataChannel
→ host validation
→ OS input adapter
→ game

## Fast input
Camera/joystick-like movement should favor newest state over an unbounded reliable event queue.

## Validation
Host validates:
- packet size
- protocol version
- sequence
- timestamp
- event type
- numeric ranges
- rate limits
- authenticated session ID

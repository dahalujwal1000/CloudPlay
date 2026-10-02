# ADR-004: DataChannel Input

Status: Accepted

## Decision
Use WebRTC DataChannels for client input.

## Reason
Input shares the authenticated peer connection and avoids introducing another transport.

## Design
Fast-changing state uses newest-state semantics where appropriate. Control messages may use reliable delivery.

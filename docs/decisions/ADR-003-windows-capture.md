# ADR-003: Windows Graphics Capture

Status: Accepted

## Decision
Use Windows Graphics Capture with D3D11 for Windows frame acquisition.

## Reason
Supported Windows capture path with a natural D3D11 pipeline into hardware encoding.

## Constraint
Handle capture permission, window destruction, resize, display changes, and graphics-device loss.

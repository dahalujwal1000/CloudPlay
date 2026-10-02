# ADR-001: WebRTC Transport

Status: Accepted

## Context
CloudPlay needs low-latency bidirectional audio/video and input data.

## Decision
Use WebRTC for media and data transport.

## Alternatives
- custom UDP
- WebSocket
- RTMP
- proprietary streaming protocol

## Consequences
Positive:
- mature real-time media architecture
- congestion handling
- ICE/STUN/TURN ecosystem
- DataChannels

Negative:
- signaling must be implemented
- WebRTC dependency is core infrastructure
- TURN infrastructure may be required

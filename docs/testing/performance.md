# Performance Test Plan

## Baseline
Record:
- PC CPU/GPU/VRAM/RAM
- game FPS without streaming
- game FPS with capture
- game FPS with encoding
- stream FPS
- RTT
- jitter
- packet loss
- end-to-end input/display latency

## Scenarios
1. LAN Ethernet
2. LAN Wi-Fi 5 GHz
3. Wi-Fi 6
4. Internet direct
5. Internet TURN
6. bandwidth reduction
7. packet loss
8. temporary disconnect
9. 2-hour session

## Acceptance direction
Streaming must not cause unacceptable game frame degradation, sustained memory growth, or unstable reconnect behavior.
Use measured thresholds defined after baseline hardware testing.

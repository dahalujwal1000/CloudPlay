# Streaming Architecture

## Initial target
- H.264
- 1920x1080
- 60 FPS
- 6–20 Mbps adaptive bitrate
- low-latency encoder settings
- no unnecessary B-frame latency

## Pipeline

D3D11 texture
→ NVENC
→ encoded H.264
→ WebRTC
→ Android WebRTC
→ MediaCodec/hardware decoder
→ Surface

## Metrics
- capture latency
- encode latency
- packetization/network delay
- RTT
- jitter
- packet loss
- decode latency
- rendered FPS
- dropped frames
- bitrate

## Adaptive bitrate
Use measured network conditions and hysteresis. Avoid rapid oscillation.

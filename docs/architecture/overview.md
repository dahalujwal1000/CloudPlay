# Architecture Overview

```text
                    SIGNALING
                 HTTPS/WSS + Auth
                       |
             +---------+---------+
             |                   |
       LINUX HOST          ANDROID CLIENT
             |                   |
       Game Manager          Session Manager
             |                   |
       Game Process             WebRTC
             |                   |
      Platform Capture      Hardware Decoder
             |                   |
         GPU Buffer         Video Surface
             |
           NVENC
             |
          WebRTC <-------> DataChannel
             |                   |
          Network           Touch/Controller
```

Media should flow peer-to-peer where possible. Signaling exchanges session metadata, SDP and ICE candidates; it should not proxy normal video.

Linux is primary under ADR-006. Portal/PipeWire capture and NVIDIA EGL imports are
implemented under ADR-007; sustained 1080p60 and NVENC interoperability are pending;
the retained Windows backend uses WGC/D3D11. Diagram components are architectural
targets, not a claim of an operational streaming pipeline.

## Session states
OFFLINE → STARTING → READY → PAIRING → CONNECTING → CONNECTED → STARTING_GAME → STREAMING → STOPPING → READY

Error states:
AUTH_FAILED, NETWORK_FAILED, GAME_START_FAILED, CAPTURE_FAILED, ENCODER_FAILED, WEBRTC_FAILED, INPUT_FAILED.

## Design principles
- explicit ownership
- explicit state
- measurable latency
- minimal copies
- authenticated control plane
- game-agnostic streaming engine

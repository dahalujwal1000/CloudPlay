# Architecture Overview

```text
                    SIGNALING
                 HTTPS/WSS + Auth
                       |
             +---------+---------+
             |                   |
       WINDOWS HOST        ANDROID CLIENT
             |                   |
       Game Manager          Session Manager
             |                   |
       Game Process             WebRTC
             |                   |
      Graphics Capture      Hardware Decoder
             |                   |
        D3D11 Texture        Video Surface
             |
           NVENC
             |
          WebRTC <-------> DataChannel
             |                   |
          Network           Touch/Controller
```

Media should flow peer-to-peer where possible. Signaling exchanges session metadata, SDP and ICE candidates; it should not proxy normal video.

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

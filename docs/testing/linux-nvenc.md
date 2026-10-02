# Linux NVIDIA Setup and Readiness

Verified locally on 2026-10-02. No CloudPlay encoder code has been implemented.

## Environment

- Fedora Linux 44 Workstation, x86-64.
- NVIDIA GeForce RTX 3050 6GB Laptop GPU, 6144 MiB VRAM.
- NVIDIA driver/KMD 615.71.09, CUDA UMD compatibility 13.4.
- Runtime libraries: `libnvidia-encode.so.1`, `libcuda.so.1`, `libnvcuvid.so.1`.
- CUDA Toolkit/nvcc is not installed. UMD version is not a toolkit version.

Sandboxed GPU access fails here; GPU checks must run with device access. Do not
mistake that restriction for a broken driver or reinstall a working driver.

## SDK Interface Setup

NVIDIA offers a separate public interface-header archive. SDK interface 13.1.15
was downloaded from NVIDIA and extracted locally under ignored `.cache/nvidia/`.
It contains `nvEncodeAPI.h`, `cuviddec.h`, and `nvcuvid.h`, not samples or CUDA.
No SDK binaries/headers are committed and no driver/system packages were changed.
The user's `/home/ujwal/Documents/Video_Codec_Interface_13.1.15.zip` was found and
its SHA-256 matches the official archive used for this setup.

Reproduce from the repository root:

```sh
curl -fL --max-time 60 https://developer.nvidia.com/downloads/designworks/video-codec-sdk/secure/13.1/video_codec_interface_13.1.15.zip -o /tmp/cloudplay-video-codec-interface.zip
printf '%s  %s\n' 830180b5a4ca15a4bf99eb94ebd669609a0e5ddca44d8cfc60eff46c3ac4ea23 /tmp/cloudplay-video-codec-interface.zip | sha256sum -c -
mkdir -p .cache/nvidia
unzip -n /tmp/cloudplay-video-codec-interface.zip -d .cache/nvidia
cmake -S . -B build/nvenc-check -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DCLOUDPLAY_NVENC_INCLUDE_DIR="$PWD/.cache/nvidia/Video_Codec_Interface_13.1.15/Interface"
cmake --build build/nvenc-check
ctest --test-dir build/nvenc-check --output-on-failure
build/nvenc-check/host/diagnostics/cloudplay_nvenc_probe
```

Stop if checksum validation fails. The checksum records the archive retrieved for
this setup; it is not a publisher signature. The probe loads the driver library,
compares its maximum NVENC API to the installed header, and verifies entry points.
It never opens an encode session or captures content; success alone proves only
API readiness. Exit codes: 0 ready/help, 1 driver/API failure, 2 invalid arguments.
Observed live probe: SDK API 13.1, driver maximum API 13.1, interface ready.
CTest also uses isolated fake libraries to check old-driver rejection, version
query failure, interface creation failure, absent functions and missing exports.
These test libraries live only under the build directory; never install them.
Local verification passed: 11 SDK-enabled CTest cases, four default CTest cases,
clang-format 18 and GCC 16 `-fanalyzer`. Linux build/tests and clang-tidy CI also
passed at revision `b85e460250a03175a33989881a132c8849a075c2` in
[readiness CI](https://github.com/dahalujwal1000/CloudPlay/actions/runs/37002479151).
Run without untrusted `LD_LIBRARY_PATH`/`LD_PRELOAD`; never point at SDK stub libraries.

The full [NVIDIA SDK download](https://developer.nvidia.com/video-codec-sdk)
requires the user's NVIDIA login and license acceptance. It has **not** been
installed. Download it yourself when samples are needed; share its local archive
path for extraction/build setup, not account credentials. A CUDA Toolkit will
also be needed to compile CUDA-based SDK samples; select it separately against
Fedora/compiler compatibility rather than installing or replacing drivers blindly.

[NVIDIA's SDK 13.1 requirements](https://forums.developer.nvidia.com/t/system-requirements-for-video-codec-sdk-v13-1/371379)
specify Linux driver 610.00 or newer. The observed driver exceeds that minimum;
the readiness probe checks the actual API rather than relying on the version alone.

## Live Hardware Checks

Installed FFmpeg exposes NVENC encoders. Listing encoders is not a hardware test.
The following synthetic test actually opened H.264 NVENC and encoded 120 frames:

```sh
nvidia-smi --query-gpu=name,driver_version,memory.total --format=csv,noheader
ffmpeg -hide_banner -nostdin -f lavfi -i testsrc2=size=1920x1080:rate=60 -frames:v 120 -an -c:v h264_nvenc -gpu 0 -preset p1 -tune ull -rc cbr -b:v 12M -maxrate 12M -bufsize 200k -bf 0 -rc-lookahead 0 -zerolatency 1 -f null -
```

Observed: exit 0, 120 H.264 Main frames, 1920x1080/60 FPS, 12 Mbps target,
about 0.34 seconds wall time (5.86x media duration). HEVC NVENC also passed for
60 synthetic 1080p frames with P1/ULL, CBR 12 Mbps and no B-frames/lookahead.
AV1 NVENC failed with `Codec not supported` and `No capable devices found`.
This tests encode, not AV1 decode support.

All output was discarded. No desktop, game, audio, private files, or user content
was captured. These short offline tests include CPU-generated input/upload and
are not a latency benchmark, sustained 1080p60 acceptance, or proof of a zero-copy
capture pipeline. FFmpeg is a diagnostic dependency, not CloudPlay's media engine.

## Next Gate

Before production encoder work, decide the Linux capture GPU-buffer path and NVENC
CUDA/OpenGL interoperability, verify synchronization/lifetimes, and arrange the
required development headers/toolkit. Preserve WebRTC and all security boundaries.
Linux support does not establish that any particular game runs on Linux; no
anti-cheat, DRM, or game compatibility workarounds are authorized.

Official API reference: [NVENC programming guide](https://docs.nvidia.com/video-technologies/video-codec-sdk/13.1/nvenc-video-encoder-api-prog-guide/index.html).

#!/bin/sh
set -eu

vendor=${CLOUDPLAY_NVIDIA_EGL_VENDOR:-/usr/share/glvnd/egl_vendor.d/10_nvidia.json}
if [ ! -r "$vendor" ]; then
    printf '%s\n' "NVIDIA EGL vendor file unavailable: $vendor" >&2
    exit 1
fi
if ! command -v ffplay >/dev/null 2>&1; then
    printf '%s\n' 'ffplay is required for the motion reference.' >&2
    exit 1
fi

# Restrict vendor selection for this process only; never change the desktop GPU.
unset DRI_PRIME
export SDL_VIDEODRIVER=wayland SDL_RENDER_DRIVER=opengl
export __NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia
export __EGL_VENDOR_LIBRARY_FILENAMES="$vendor"
export __VK_LAYER_NV_optimus=NVIDIA_only
exec ffplay -f lavfi -i testsrc2=size=1920x1080:rate=60 -an -fs \
    -loglevel verbose -stats -window_title 'CloudPlay Motion Reference' "$@"

#!/bin/sh
# Build rpc-gpu-probe for the handheld with the Buildroot cross toolchain of the retroid-pocket-classic output tree,
# and drop it where it can be pushed to /data with scp.
#   tools/rpc-probe-build.sh [output-dir]      default: output/retroid-pocket-classic
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
out=${1:-$here/output/retroid-pocket-classic}
host=$out/host
[ -x "$host/bin/aarch64-linux-gcc" ] || { echo "no cross compiler in $host (build the image first)" >&2; exit 1; }
[ -d "$out/target/usr/lib" ] || { echo "no $out/target/usr/lib (mesa not installed?)" >&2; exit 1; }

# the sysroot has Mesa's EGL/GLES2 headers and libdrm/gbm; -Wl,-rpath keeps the binary self-explanatory on the card
CC="$host/bin/aarch64-linux-gcc"
SYS=$host/aarch64-buildroot-linux-gnu/sysroot
mkdir -p /tmp/opencode/rpc
$CC -O2 -Wall -Wextra -o /tmp/opencode/rpc/rpc-gpu-probe \
    -I"$SYS/usr/include" -I"$SYS/usr/include/libdrm" \
    "$here/tools/rpc-gpu-probe.c" \
    -L"$SYS/usr/lib" -lEGL -lGLESv2 -lgbm -ldrm \
    -Wl,-rpath,/usr/lib
echo "built: /tmp/opencode/rpc/rpc-gpu-probe"

# route-probe: what the ALSA mixer API makes of the DSP's route switches (the menu writes them with it), for the
# card whose DSP rebuilds its controls with each graph's life -- the reason ui/src/sound.cpp re-reads the mixer
$CC -O2 -Wall -Wextra -o /tmp/opencode/rpc/route-probe \
    -I"$SYS/usr/include" "$here/tools/route-probe.c" \
    -L"$SYS/usr/lib" -lasound -Wl,-rpath,/usr/lib
echo "built: /tmp/opencode/rpc/route-probe"
file /tmp/opencode/rpc/rpc-gpu-probe 2>/dev/null || true

#!/bin/sh
# The hash of everything wasm/banana-engine.js is built from (the engine's sources, the binding, the build script and
# the embedded fonts, icons and library): build.sh writes it into the output, tests/run.sh compares.
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
cd "$ROOT" || exit 1
export LC_ALL=C
{
    ls ui/src/layout.cpp ui/src/expr.cpp ui/src/json.cpp ui/src/library.cpp ui/src/theme.cpp ui/src/soft.cpp ui/src/zip.cpp \
        ui/rnds/RndsRaster.cpp ui/rnds/RndsUI.cpp ui/src/*.h ui/rnds/*.h ui/tests/fixture.sh system/rootfs/etc/banana/systems.conf
    find ui/assets/rnds/fonts ui/assets/rnds/icons ui/assets/banana -type f
    ls ui/assets/rnds/systems.cfg ui/assets/rnds/*.png tools/theme-studio/wasm/theme-wasm.cpp tools/theme-studio/wasm/build.sh
} | sort -u | while read -r f; do printf '%s\n' "$f"; cat "$f"; done | sha256sum | cut -c1-16

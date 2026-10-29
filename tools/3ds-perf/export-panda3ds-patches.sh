#!/bin/sh
# BananaOS's Panda3DS work as the patches buildroot/package/libretro-panda3ds applies: one for Panda3DS (from the
# pinned upstream commit to the branch), one for its dynarmic submodule (paths under third_party/dynarmic, as the
# package's source tree has them).
#
#   tools/3ds-perf/export-panda3ds-patches.sh <panda3ds checkout> [branch, default banana3ds]
set -e
SRC=$1; BRANCH=${2:-banana3ds}
[ -d "$SRC/.git" ] || { echo "usage: $0 <panda3ds checkout> [branch]" >&2; exit 2; }
HERE=$(cd "$(dirname "$0")/../.." && pwd)
PKG=$HERE/buildroot/package/libretro-panda3ds
UPSTREAM=$(sed -n 's/^LIBRETRO_PANDA3DS_VERSION = //p' "$PKG/libretro-panda3ds.mk")
DYN_UPSTREAM=$(git -C "$SRC" ls-tree "$UPSTREAM" third_party/dynarmic | awk '{print $3}')
DYN_BRANCH=$(git -C "$SRC" ls-tree "$BRANCH" third_party/dynarmic | awk '{print $3}')

rm -f "$PKG"/*.patch
{
    echo "BananaOS's speed work on Panda3DS for the RG DS Plus (Cortex-A55), from branch $BRANCH."
    echo "PANDA3DS_BASELINE=1 in the environment runs upstream's code paths instead. Measured with tools/3ds-perf."
    echo
    git -C "$SRC" log --format='  - %s' --no-merges "$UPSTREAM..$BRANCH"
    echo
    git -C "$SRC" diff "$UPSTREAM" "$BRANCH" -- . ':!third_party/dynarmic'
} > "$PKG/0001-banana-speed-work.patch"
if [ "$DYN_UPSTREAM" != "$DYN_BRANCH" ]; then
    {
        echo "BananaOS's changes to dynarmic's arm64 backend (the ARM11's JIT on the Cortex-A55)."
        echo
        git -C "$SRC/third_party/dynarmic" log --format='  - %s' "$DYN_UPSTREAM..$DYN_BRANCH"
        echo
        git -C "$SRC/third_party/dynarmic" diff --src-prefix=a/third_party/dynarmic/ --dst-prefix=b/third_party/dynarmic/ \
            "$DYN_UPSTREAM" "$DYN_BRANCH"
    } > "$PKG/0002-dynarmic-arm64-a32.patch"
fi
ls -la "$PKG"/*.patch

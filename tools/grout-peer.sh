#!/bin/sh
# Build ui/tests/grout-peer (Grout's own save sync, run headless on a test card) inside a Grout checkout:
#
#   sh tools/grout-peer.sh <grout checkout> <output binary>
#
# The checkout must be at the commit in ui/tests/GROUT: the interop table was checked against that Grout, and another
# may sync differently. The harness goes in through `go build -overlay`, so nothing is written into the checkout. Grout's
# UI library links SDL2 through cgo even though the sync never opens a window, so this needs Go (as Grout's go.mod
# asks) and the SDL2 development packages (on Debian or Ubuntu: libsdl2-dev, libsdl2-ttf-dev, libsdl2-image-dev,
# libsdl2-gfx-dev). The version stamped in is the one in the checkout's pak.json, as Grout's own build stamps it.
set -eu
[ $# -eq 2 ] || { echo "usage: $0 <grout checkout> <output binary>" >&2; exit 2; }
SRC=$(cd "$1" && pwd)
OUTBIN=$2
HERE=$(cd "$(dirname "$0")/.." && pwd)
PIN=$(sed -n 's/^\([0-9a-f]\{40\}\).*/\1/p' "$HERE/ui/tests/GROUT")
AT=$(git -C "$SRC" rev-parse HEAD)
[ "$AT" = "$PIN" ] || { echo "grout-peer: $SRC is at $AT, not the pinned $PIN (ui/tests/GROUT)" >&2; exit 1; }
command -v go >/dev/null || { echo "grout-peer: no go on PATH" >&2; exit 1; }
pkg-config --exists sdl2 SDL2_ttf SDL2_image SDL2_gfx || { echo "grout-peer: the SDL2 development packages are missing" >&2; exit 1; }
VER=$(sed -n 's/^ *"version": *"\([^"]*\)".*/\1/p' "$SRC/pak.json" | head -1)
TMP=$(mktemp -d)
trap 'rm -f "$TMP/overlay.json"; rmdir "$TMP"' EXIT
printf '{"Replace":{"%s":"%s"}}\n' "$SRC/tools/grout-peer/main.go" "$HERE/ui/tests/grout-peer/main.go" > "$TMP/overlay.json"
case $OUTBIN in /*) ;; *) OUTBIN=$PWD/$OUTBIN ;; esac
cd "$SRC"
GOWORK=off go build -overlay "$TMP/overlay.json" -ldflags "-X 'grout/version.Version=$VER'" -o "$OUTBIN" ./tools/grout-peer

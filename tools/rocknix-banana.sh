#!/bin/sh
# Puts BananaOS's menu into a ROCKNIX checkout as the frontend of a dual-screen device (the AYN Thor: SM8550): the
# banana-frontend package (rocknix/ in this repository) with banana-ui's source beside it, and the package added to the
# device's ADDITIONAL_PACKAGES. Then ROCKNIX's own build makes the image: make docker-SM8550 (see its README).
# Run again after a change to ui/ to bring the new source over; idempotent.
#
#   tools/rocknix-banana.sh <ROCKNIX checkout> [device: SM8550]
set -e
HERE=$(cd "$(dirname "$0")/.." && pwd)
R=${1:?usage: rocknix-banana.sh <ROCKNIX checkout> [device]}
DEV=${2:-SM8550}
[ -d "$R/projects/ROCKNIX/devices/$DEV" ] || { echo "no ROCKNIX device $DEV in $R"; exit 1; }
P=$R/projects/ROCKNIX/packages/ui/banana-frontend
mkdir -p "$P"
cp -r "$HERE/rocknix/packages/ui/banana-frontend/." "$P/"
# banana-ui's source, without what a build left (binaries, test output)
rm -rf "$P/sources/ui"
mkdir -p "$P/sources/ui"
(cd "$HERE/ui" && tar --exclude='./tests/out' --exclude='./banana-ui' --exclude='./banana-*' --exclude='*.o' -cf - .) |
    tar -C "$P/sources/ui" -xf -
O=$R/projects/ROCKNIX/devices/$DEV/options
grep -q 'banana-frontend' "$O" ||
    sed -i 's/^\([[:space:]]*ADDITIONAL_PACKAGES="\)/\1banana-frontend /' "$O"
grep -q 'ADDITIONAL_PACKAGES=".*banana-frontend' "$O" || { echo "couldn't add banana-frontend to $O"; exit 1; }
echo "banana-frontend in $R ($DEV): build with ROCKNIX's make docker-$DEV"

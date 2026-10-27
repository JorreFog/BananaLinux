#!/bin/sh
# Write a BananaOS card image to an SD card, checking it is the right card first: the device must be removable, big
# enough for the image, and not hold anything mounted. Then the image goes on with dd and is read back and compared,
# sector by sector (a card that lies about writes is the usual cause of a boot that fails halfway).
#
#   sudo tools/write-sd.sh <banana-*.img> /dev/sdX        (the .gz: tools/write-sd.sh <img.gz> /dev/sdX works too)
#
# The card is destroyed, so the script asks for the device's size and model and wants "yes" typed back. Everything
# after that is the write, the sync and the check; it takes a few minutes for a 1.4 GB image on a slow card.
set -e
IMG=$1
DEV=$2
[ -n "$IMG" ] && [ -f "$IMG" ] || { echo "usage: $0 <image[.gz]> /dev/sdX" >&2; exit 2; }
[ -n "$DEV" ] && [ -b "$DEV" ] || { echo "usage: $0 <image[.gz]> /dev/sdX (a block device)" >&2; exit 2; }
[ "$(id -u)" = 0 ] || { echo "run it as root: sudo $0 $IMG $DEV" >&2; exit 2; }
case $DEV in /dev/nvme*|/dev/mmcblk0*) echo "$DEV looks like the machine's own disk, not a card: refusing" >&2; exit 1 ;; esac

NAME=$(basename "$DEV")
REMOVABLE=$(cat "/sys/block/$NAME/removable" 2>/dev/null || echo 0)
SIZE=$(blockdev --getsize64 "$DEV")
MODEL=$(cat "/sys/block/$NAME/device/model" 2>/dev/null | tr -s ' ' || echo "?")
if [ "$REMOVABLE" != 1 ]; then
    echo "warning: $DEV doesn't report itself as removable ($MODEL)"
fi
if [ "$(lsblk -no MOUNTPOINT "$DEV" 2>/dev/null | grep -c .)" != 0 ]; then
    echo "$DEV has something mounted on it; unmount it first:" >&2
    lsblk "$DEV" >&2
    exit 1
fi

if [ "${IMG%.gz}" != "$IMG" ]; then
    # unpack beside the image (same filesystem, room for it), not in a tmpfs that may not hold 1.4 GB
    PLAIN=$(dirname "$IMG")/.banana-sd-$$.img
    echo "unpacking $IMG"
    gunzip -c "$IMG" > "$PLAIN"
    CLEANUP=$PLAIN
else
    PLAIN=$IMG
    CLEANUP=
fi
trap 'rm -f "$CLEANUP"' EXIT INT TERM
NEED=$(stat -c %s "$PLAIN")
[ "$SIZE" -ge "$NEED" ] || { echo "$DEV is $((SIZE / 1024 / 1024)) MB, the image is $((NEED / 1024 / 1024)) MB: too small" >&2; exit 1; }

echo "card:  $DEV, $MODEL, $((SIZE / 1024 / 1024)) MB"
echo "image: $PLAIN, $((NEED / 1024 / 1024)) MB"
printf 'everything on %s is erased. type "yes" to write: ' "$DEV"
read -r ANSWER
[ "$ANSWER" = yes ] || { echo "nothing written"; exit 1; }

echo "writing"
dd if="$PLAIN" of="$DEV" bs=4M conv=fsync status=progress
sync
echo "checking the card reads back what was written"
if cmp -n "$NEED" "$PLAIN" "$DEV"; then
    echo "OK: $DEV holds the image; it can go in the handheld"
else
    echo "the card doesn't read back the image: try another card or another reader" >&2
    exit 1
fi

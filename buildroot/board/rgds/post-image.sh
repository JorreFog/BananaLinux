#!/bin/sh
# Buildroot post-image: the SD card image for one handheld, or for both (universal: both device trees in each slot,
# and extlinux.conf's FDTDIR lets U-Boot pick the one for the handheld it finds itself in, $fdtfile, which its
# preboot sets from the board's ID voltage: uboot-banana.fragment).
#   post-image.sh <images dir> <board: rgds | rgds-plus | universal>
set -e
BOARD=${2:-rgds}
case $BOARD in
rgds) DTBS=rk3568-anbernic-rg-ds ;;
rgds-plus) DTBS=rk3568-anbernic-rg-ds-plus ;;
universal) DTBS="rk3568-anbernic-rg-ds rk3568-anbernic-rg-ds-plus" ;;
*) echo "unknown board $BOARD"; exit 1 ;;
esac
HERE=$(dirname "$0")
cd "$BINARIES_DIR"
if [ $BOARD = universal ]; then
    # FDTDIR /a/: U-Boot loads /a/$fdtfile; what it found is passed on (banana.board=, banana.adc=) for the logs
    sed -e 's|^  FDT /\([ab]\)/@DTB@.dtb|  FDTDIR /\1/|' \
        -e 's|^\(  APPEND .*\)$|\1 banana.board=${banana_board} banana.adc=${banana_adc}|' "$HERE/extlinux.conf.in" > extlinux.conf
else
    sed "s/@DTB@/$DTBS/g" "$HERE/extlinux.conf.in" > extlinux.conf
fi
FILES=; for d in $DTBS; do FILES="$FILES\t\tfile a/$d.dtb { image = \"$d.dtb\" }\n"; done
sed -e "s|^.*@DTBFILES@.*$|$FILES|" -e "s/@BOARD@/$BOARD/g" "$HERE/genimage.cfg.in" > genimage.cfg
rm -rf "$BUILD_DIR/genimage.tmp"
genimage --rootpath "$TARGET_DIR" --tmppath "$BUILD_DIR/genimage.tmp" --inputpath "$BINARIES_DIR" \
    --outputpath "$BINARIES_DIR" --config genimage.cfg
gzip -kf9 "banana-$BOARD.img"
# the same system as an update package (Settings > System > Update reads it from banana/updates/ on the card)
VERSION=$(cat "$HERE/../../../VERSION")
U=banana-$BOARD-$VERSION.update
rm -rf "$U"; mkdir "$U"
cp Image rootfs.squashfs "$U/"; for d in $DTBS; do cp "$d.dtb" "$U/"; done
echo "$VERSION" > "$U/VERSION"; echo "$BOARD" > "$U/board"
(cd "$U" && sha256sum Image *.dtb rootfs.squashfs > sha256sums)
tar -cf "$U.tar" "$U"
rm -rf "$U"
echo "SD card image: $BINARIES_DIR/banana-$BOARD.img(.gz); update package: $BINARIES_DIR/$U.tar"

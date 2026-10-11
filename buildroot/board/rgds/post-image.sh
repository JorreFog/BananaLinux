#!/bin/sh
# Buildroot post-image: the SD card image for one handheld.
#   post-image.sh <images dir> <board: rgds | rgds-plus>
set -e
BOARD=${2:-rgds}
case $BOARD in
rgds) DTB=rk3568-anbernic-rg-ds ;;
rgds-plus) DTB=rk3568-anbernic-rg-ds-plus ;;
*) echo "unknown board $BOARD"; exit 1 ;;
esac
HERE=$(dirname "$0")
cd "$BINARIES_DIR"
sed "s/@DTB@/$DTB/" "$HERE/extlinux.conf.in" > extlinux.conf
sed -e "s/@DTB@/$DTB/" -e "s/@BOARD@/$BOARD/" "$HERE/genimage.cfg.in" > genimage.cfg
rm -rf "$BUILD_DIR/genimage.tmp"
genimage --rootpath "$TARGET_DIR" --tmppath "$BUILD_DIR/genimage.tmp" --inputpath "$BINARIES_DIR" \
    --outputpath "$BINARIES_DIR" --config genimage.cfg
gzip -kf9 "banana-$BOARD.img"
echo "SD card image: $BINARIES_DIR/banana-$BOARD.img(.gz)"

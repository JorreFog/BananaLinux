#!/bin/sh
# Buildroot post-image: the SD card image for one handheld.
#   post-image.sh <images dir> <board: rgds | rgds-plus>
set -e
BOARD=${2:-rgds}
case $BOARD in
rgds) DTBS=rk3568-anbernic-rg-ds ;;
rgds-plus) DTBS=rk3568-anbernic-rg-ds-plus ;;
*) echo "unknown board $BOARD"; exit 1 ;;
esac
HERE=$(dirname "$0")
cd "$BINARIES_DIR"
sed "s/@DTB@/$DTBS/g" "$HERE/extlinux.conf.in" > extlinux.conf
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

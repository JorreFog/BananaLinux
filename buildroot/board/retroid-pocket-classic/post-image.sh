#!/bin/sh
# Buildroot post-image: Banana SM4450 SD image with OpenABL (no U-Boot).
#   post-image.sh <images dir> <board>
set -e
BOARD=${2:-retroid-pocket-classic}
DTB=sm4450-retroid-pocket-classic-revq
HERE=$(dirname "$0")
cd "$BINARIES_DIR"

sed "s/@DTB@/$DTB/g" "$HERE/extlinux.conf.in" > extlinux.conf

FILES=
FILES="$FILES\t\tfile a/Image { image = \"Image\" }\n"
FILES="$FILES\t\tfile a/$DTB.dtb { image = \"$DTB.dtb\" }\n"
FILES="$FILES\t\tfile extlinux/extlinux.conf { image = \"extlinux.conf\" }\n"
sed -e "s|^.*@DTBFILES@.*$|$FILES|" -e "s/@BOARD@/$BOARD/g" "$HERE/genimage.cfg.in" > genimage.cfg

rm -rf "$BUILD_DIR/genimage.tmp"
genimage --rootpath "$TARGET_DIR" --tmppath "$BUILD_DIR/genimage.tmp" \
    --inputpath "$BINARIES_DIR" --outputpath "$BINARIES_DIR" --config genimage.cfg
gzip -kf9 "banana-$BOARD.img"

VERSION=$(cat "$HERE/../../../VERSION")
U=banana-$BOARD-$VERSION.update
rm -rf "$U"; mkdir "$U"
cp Image "$DTB.dtb" rootfs.squashfs "$U/"
echo "$VERSION" > "$U/VERSION"; echo "$BOARD" > "$U/board"
(cd "$U" && sha256sum Image "$DTB.dtb" rootfs.squashfs > sha256sums)
tar -cf "$U.tar" "$U"; rm -rf "$U"
echo "image: $BINARIES_DIR/banana-$BOARD.img(.gz); update: $BINARIES_DIR/$U.tar"

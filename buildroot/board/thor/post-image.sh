#!/bin/sh
# Buildroot post-image for the AYN Thor: the SD card image and the update package, as board/rgds/post-image.sh makes
# the RG DS's (the same partitions, slots and package; see there). What differs: no U-Boot on the card (OpenABL, on
# the Thor's own flash, reads the boot partition), the kernel an uncompressed EFI-stub Image (OpenABL starts it as an
# EFI application; no LZ4), the boot entry both as extlinux.conf and as a Boot Loader Specification entry
# (loader/entries/banana.conf: OpenABL reads either), and the device tree a second time in dtbs/ for OpenABL's
# device-model menu.
#   post-image.sh <images dir> thor
set -e
BOARD=${2:-thor}
DTB=qcs8550-ayn-thor
HERE=$(dirname "$0")
VERSION=$(cat "$HERE/../../../VERSION")
cd "$BINARIES_DIR"
[ -f "$DTB.dtb" ] || { echo "post-image: no $DTB.dtb in $BINARIES_DIR"; exit 1; }
[ -f openabl/abl_signed-SM8550.elf ] || { echo "post-image: no OpenABL (BR2_PACKAGE_OPENABL)"; exit 1; }
sed "s/@DTB@/$DTB/g" "$HERE/extlinux.conf.in" > extlinux.conf
sed -e "s/@DTB@/$DTB/g" -e "s/@VERSION@/$VERSION/g" "$HERE/banana.conf.in" > banana.conf
cp "$HERE/openabl-README.txt" openabl-README.txt
FILES="\t\tfile a/$DTB.dtb { image = \"$DTB.dtb\" }\n\t\tfile dtbs/qcom/$DTB.dtb { image = \"$DTB.dtb\" }\n"
sed -e "s|^.*@DTBFILES@.*$|$FILES|" -e "s/@BOARD@/$BOARD/g" "$HERE/genimage.cfg.in" > genimage.cfg
rm -rf "$BUILD_DIR/genimage.tmp"
genimage --rootpath "$TARGET_DIR" --tmppath "$BUILD_DIR/genimage.tmp" --inputpath "$BINARIES_DIR" \
    --outputpath "$BINARIES_DIR" --config genimage.cfg
gzip -kf9 "banana-$BOARD.img"
# the update package: the same files as the RG DS's (update apply copies Image and the .dtb to the other slot)
U=banana-$BOARD-$VERSION.update
rm -rf "$U"; mkdir "$U"
cp Image rootfs.squashfs "$DTB.dtb" "$U/"
echo "$VERSION" > "$U/VERSION"; echo "$BOARD" > "$U/board"
(cd "$U" && sha256sum Image *.dtb rootfs.squashfs > sha256sums)
tar -cf "$U.tar" "$U"
rm -rf "$U"
echo "SD card image: $BINARIES_DIR/banana-$BOARD.img(.gz); update package: $BINARIES_DIR/$U.tar"

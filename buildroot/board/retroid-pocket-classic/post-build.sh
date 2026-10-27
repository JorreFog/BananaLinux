#!/bin/sh
# Buildroot post-build for the Retroid Pocket Classic (SM4450 Rev Q).
set -e
T=$1
HERE=$(dirname "$0")
BOARD=retroid-pocket-classic

# Banana's init scripts replace Buildroot's S* scripts (rcS starts everything).
rm -f "$T"/etc/init.d/S[0-9]*

mkdir -p "$T/data" "$T/run" "$T/boot"

cp "$HERE/../../../VERSION" "$T/etc/banana/version"
echo "$BOARD" > "$T/etc/banana/board"

V=$(cat "$HERE/../../../VERSION")
printf 'NAME=BananaOS\nID=bananaos\nID_LIKE=buildroot\nVERSION="%s"\nVERSION_ID=%s\nPRETTY_NAME="BananaOS %s"\nHOME_URL="https://github.com/JorreFog/BananaLinux"\n' \
    "$V" "$V" "$V" > "$T/usr/lib/os-release"
ln -sf ../usr/lib/os-release "$T/etc/os-release"
ln -sfn data/roms "$T/roms"

# SM4450 firmware: ADSP/WPSS/AW88166 ACF, GPU zap, QCA BT.
if [ -d "$HERE/firmware" ]; then
    mkdir -p "$T/lib/firmware"
    cp -a "$HERE/firmware/." "$T/lib/firmware/"
fi

rm -rf "$T/usr/share/man" "$T/usr/share/doc" "$T/usr/share/info"

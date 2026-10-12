#!/bin/sh
# Buildroot post-build: what the root filesystem doesn't need on a handheld that boots to one program.
#   post-build.sh <target dir> <board: rgds | rgds-plus>
set -e
T=$1
# init's own scripts replace Buildroot's (rcS here starts everything)
rm -f "$T"/etc/init.d/S[0-9]*
# the data partition's mount point, and the boot partition's
mkdir -p "$T/data" "$T/run" "$T/boot"
# the version the settings screen's About shows, and the board (an update package must be for the same one)
cp "$(dirname "$0")/../../../VERSION" "$T/etc/banana/version"
echo "${2:-rgds}" > "$T/etc/banana/board"
# who we are, for programs that ask (PortMaster names the firmware from it)
V=$(cat "$(dirname "$0")/../../../VERSION")
printf 'NAME=BananaOS\nID=bananaos\nID_LIKE=buildroot\nVERSION="%s"\nVERSION_ID=%s\nPRETTY_NAME="BananaOS %s"\nHOME_URL="https://github.com/JorreFog/BananaLinux"\n' \
    "$V" "$V" "$V" > "$T/usr/lib/os-release"
ln -sf ../usr/lib/os-release "$T/etc/os-release"
# /roms: the card's games where ports and PortMaster look for them (/roms/ports/PortMaster), as on other firmware
ln -sfn data/roms "$T/roms"
rm -rf "$T/usr/share/man" "$T/usr/share/doc" "$T/usr/share/info"

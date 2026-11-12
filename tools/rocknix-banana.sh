#!/bin/sh
# Puts BananaOS into a ROCKNIX checkout for a dual-screen device (the AYN Thor: SM8550): the banana-frontend package
# (rocknix/ in this repository: the menu as the frontend, banana-ui's and Activity's source beside it), Banana's own
# emulators (BananaDS, BananaFront, Game & Watch through MAME, the BananaGBA/NES/SNES cores) and apps (the Store, Döda
# Kvarter) as ROCKNIX packages with BananaOS's patches, Banana's RetroArch patches that fit ROCKNIX's RetroArch, and
# everything listed where ROCKNIX's build looks (tools/rocknix-register.py: the device's ADDITIONAL_PACKAGES, the
# emulator lists, es_features.cfg). Then ROCKNIX's own build makes the image: make docker-SM8550 (see its README).
# Run again after a change here to bring it over; idempotent.
#
#   tools/rocknix-banana.sh <ROCKNIX checkout> [device: SM8550]
set -e
HERE=$(cd "$(dirname "$0")/.." && pwd)
R=${1:?usage: rocknix-banana.sh <ROCKNIX checkout> [device]}
DEV=${2:-SM8550}
[ -d "$R/projects/ROCKNIX/devices/$DEV" ] || { echo "no ROCKNIX device $DEV in $R"; exit 1; }
PK=$R/projects/ROCKNIX/packages

# ---- the frontend ----
P=$PK/ui/banana-frontend
mkdir -p "$P"
cp -r "$HERE/rocknix/packages/ui/banana-frontend/." "$P/"
# banana-ui's source, without what a build left (binaries, test output); Activity's
rm -rf "$P/sources/ui" "$P/sources/activity"
mkdir -p "$P/sources/ui" "$P/sources/activity"
(cd "$HERE/ui" && tar --exclude='./tests/out' --exclude='./banana-ui' --exclude='./banana-*' --exclude='*.o' -cf - .) |
    tar -C "$P/sources/ui" -xf -
cp "$HERE/store/apps/activity/banana-activity.cpp" "$HERE/store/apps/activity/activity-font.h" \
    "$HERE/store/apps/activity/activity-logo.h" "$P/sources/activity/"
# BananaOS's system scripts the menu runs, in their services mode on ROCKNIX (see the package), and its apps' launchers
rm -rf "$P/sources/libexec" "$P/sources/sleep.d"
mkdir -p "$P/sources/libexec" "$P/sources/sleep.d"
for f in wifi wifi-nm bluetooth volume update ssh share sd2 sound-server hw-probe backlight run-rocknix rocknix-options handheld \
         ra-reset ra-secrets nds-saves nds-reset nds-buttons nds-game-option nds-game-shader saves-flat systems-merge kv-env \
         run-store run-store-app run-dodakvarter run-activity run-bank; do
    [ -f "$HERE/system/rootfs/usr/libexec/banana/$f" ] && cp "$HERE/system/rootfs/usr/libexec/banana/$f" "$P/sources/libexec/"
done
cp "$HERE/system/rootfs/etc/banana/sleep.d/20-sound" "$P/sources/sleep.d/"

# ---- Banana's emulators and apps ----
for d in "$HERE"/rocknix/packages/emulators/libretro/* "$HERE"/rocknix/packages/emulators/standalone/*; do
    t=$PK/emulators/$(basename "$(dirname "$d")")/$(basename "$d")
    rm -rf "$t"; mkdir -p "$t"; cp -r "$d/." "$t/"
done
A=$PK/apps/banana-apps
rm -rf "$A"; mkdir -p "$A"; cp -r "$HERE/rocknix/packages/apps/banana-apps/." "$A/"
# (BananaOS's patches, kept once in buildroot/patches: the Store's as 0001-0003 under the two-window one, MAME's)
cp "$HERE"/buildroot/patches/rocknixds-store/*.patch "$A/patches/"
mkdir -p "$PK/emulators/standalone/mame-gnw/patches"
cp "$HERE"/buildroot/patches/mame-gnw/*.patch "$PK/emulators/standalone/mame-gnw/patches/"
# the Store's catalog and its apps' tiles, the tiles' art and descriptions
mkdir -p "$A/sources/store/apps" "$A/sources/dodakvarter"
cp "$HERE/store/catalog.json" "$HERE/buildroot/package/rocknixds-store/store-image.png" \
    "$HERE/buildroot/package/rocknixds-store/gamelist.xml" "$A/sources/store/"
for d in "$HERE"/store/apps/*/; do
    ls "$d"*.png >/dev/null 2>&1 || continue
    mkdir -p "$A/sources/store/apps/$(basename "$d")"; cp "$d"*.png "$A/sources/store/apps/$(basename "$d")/"
done
cp "$HERE/buildroot/package/dodakvarter/gamelist.xml" "$A/sources/dodakvarter/"

# ---- Banana's RetroArch patches, as ROCKNIX's RetroArch takes them: ALSA's configuration read again at a driver's
# reopening (0001's part it lacks: it has the AUDIO_REINIT command), RGUI in Ukrainian (0005, as it is). 0006 (the
# RetroAchievements memory chains) is in its rcheevos already; 0002-0004 (the DRM context, the card's updater, the
# image's cores) are BananaOS's own ----
RA=$PK/emulators/libretro/retroarch/patches
if [ -d "$RA" ]; then
    cp "$HERE"/rocknix/patches/retroarch/*.patch "$RA/"
    cp "$HERE/buildroot/patches/retroarch/0005-menu-rgui-Ukrainian-with-the-Cyrillic-font.patch" "$RA/1005-banana-rgui-ukrainian.patch"
fi

# ---- listed where ROCKNIX's build looks ----
python3 "$HERE/tools/rocknix-register.py" "$R" "$DEV"
grep -q 'ADDITIONAL_PACKAGES=".*banana-frontend' "$R/projects/ROCKNIX/devices/$DEV/options" ||
    { echo "couldn't add banana-frontend to the $DEV options"; exit 1; }
echo "BananaOS in $R ($DEV): build with ROCKNIX's make docker-$DEV"

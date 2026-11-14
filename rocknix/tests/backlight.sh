#!/bin/sh
# usr/libexec/banana/backlight against made-up /sys trees: the AYN Thor's two DSI panels' backlights (which is the top,
# by the DSI hosts' order and BANANA_TOP), the RG DS's backlight0/1, levels set and kept per screen, a screen off and
# on for a game, ROCKNIX's per-screen settings kept the same.
HERE=$(cd "$(dirname "$0")/.." && pwd)
BL=$HERE/../system/rootfs/usr/libexec/banana/backlight
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
fail=0
bad() { echo "FAIL backlight: $*"; fail=1; }
# mk <root> <name> <device path under root> <max>
mk() {
    mkdir -p "$1/devices/$3" "$1/class/backlight"
    d=$1/devices/$3/backlight/$2; mkdir -p "$d"
    echo $4 > "$d/max_brightness"; echo $(( $4 / 2 )) > "$d/brightness"
    ln -s "$d" "$1/class/backlight/$2"; ln -s "$1/devices/$3" "$d/device"
}
# the Thor: its bottom panel on the first DSI host (ae94000.dsi), its top on the second (ae96000.dsi); the backlights'
# names don't sort the way the hosts do
mk "$T/thor" ae96000.dsi.0 platform/soc/ae00000.display-subsystem/ae96000.dsi/ae96000.dsi.0 4095
mk "$T/thor" ae94000.dsi.0 platform/soc/ae00000.display-subsystem/ae94000.dsi/ae94000.dsi.0 2047
printf 'HW_SERVICES=systemd\n' > "$T/hw.conf"
run() { env BANANA_SYS="$T/thor" BANANA_DATA="$T/data" BANANA_RUN="$T/run" BANANA_HW_CONF="$T/hw.conf" \
    ROCKNIX_SETTINGS="$T/system.cfg" BANANA_TOP=DSI-2 sh "$BL" "$@"; }
[ "$(run names)" = "ae96000.dsi.0 ae94000.dsi.0" ] || bad "the Thor's names: $(run names)"
[ "$(env BANANA_SYS="$T/thor" BANANA_TOP=DSI-1 sh "$BL" names)" = "ae94000.dsi.0 ae96000.dsi.0" ] || bad "DSI-1 as the top"
run set 0 800; run set 1 300
[ "$(cat "$T/thor/class/backlight/ae96000.dsi.0/brightness")" = 3276 ] || bad "the top's level: $(cat "$T/thor/class/backlight/ae96000.dsi.0/brightness")"
[ "$(cat "$T/thor/class/backlight/ae94000.dsi.0/brightness")" = 614 ] || bad "the bottom's level"
[ "$(cat "$T/data/backlight")" = "800 300" ] || bad "banana/backlight: $(cat "$T/data/backlight")"
[ "$(run get 0)" = 800 ] || bad "get 0: $(run get 0)"
# ROCKNIX's screens in /sys/class/backlight's order: ae94000 (the bottom) first
grep -qx 'display.brightness=30' "$T/system.cfg" && grep -qx 'display.brightness2=80' "$T/system.cfg" ||
    bad "ROCKNIX's settings: $(tr '\n' ' ' < "$T/system.cfg")"
run off 1; [ "$(cat "$T/thor/class/backlight/ae94000.dsi.0/brightness")" = 0 ] || bad "off 1"
run on 1; [ "$(cat "$T/thor/class/backlight/ae94000.dsi.0/brightness")" = 614 ] || bad "on 1 didn't restore"
echo 1 > "$T/thor/class/backlight/ae96000.dsi.0/brightness"; run restore
[ "$(cat "$T/thor/class/backlight/ae96000.dsi.0/brightness")" = 3276 ] || bad "restore"
# the RG DS: backlight0 and backlight1 under no DSI host, backlight1 the top
mk "$T/rgds" backlight0 platform/backlight0 255
mk "$T/rgds" backlight1 platform/backlight1 255
[ "$(env BANANA_SYS="$T/rgds" sh "$BL" names)" = "backlight1 backlight0" ] || bad "the RG DS's names"
# the profile's names win
[ "$(env BANANA_SYS="$T/rgds" BANANA_BACKLIGHT_TOP=a BANANA_BACKLIGHT_BOTTOM=b sh "$BL" names)" = "a b" ] || bad "the profile's names"
[ $fail = 0 ] && echo "backlight: all passed"
exit $fail

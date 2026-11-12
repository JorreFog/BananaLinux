#!/bin/sh
# usr/libexec/banana/usb-debug against a made-up configfs (plain folders: no kernel, nothing started): only where the
# board's profile asks and Settings hasn't said no; the gadget's tree (its ids, the three functions linked into its
# configuration, adb only with adbd in the image); a kernel without a function goes on with the others; down
# leaves nothing.
HERE=$(cd "$(dirname "$0")/.." && pwd)
U=$HERE/rootfs/usr/libexec/banana/usb-debug
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
fail=0
bad() { echo "FAIL usb-debug: $*"; fail=1; }
mkdir -p "$T/cfs/usb_gadget" "$T/sys/class/udc/a600000.usb" "$T/data" "$T/run"
export BANANA_CONFIGFS="$T/cfs" BANANA_SYS="$T/sys" BANANA_RUN="$T/run" BANANA_DATA="$T/data" BANANA_USB_DRY=1 BANANA_HW_CONF="$T/hw.conf"
G=$T/cfs/usb_gadget/banana
# a board that doesn't ask: nothing
echo 'HW_NAME="RG DS"' > "$T/hw.conf"
sh "$U" up >/dev/null; [ -e "$G" ] && bad "a gadget on a board that didn't ask for one"
# the Thor's profile, but Settings said no
printf 'HW_NAME="AYN Thor"\nHW_USB_DEBUG=yes\n' > "$T/hw.conf"
echo "usb_debug = off" > "$T/data/settings.conf"
sh "$U" up >/dev/null; [ -e "$G" ] && bad "a gadget with usb_debug = off"
rm -f "$T/data/settings.conf"
# on, with adbd in the image
BANANA_USB_ADB=1 sh "$U" up >/dev/null || bad "up failed"
[ "$(cat "$G/idVendor")" = 0x1d6b ] && [ "$(cat "$G/bDeviceClass")" = 0xEF ] || bad "the gadget's ids"
[ "$(cat "$G/strings/0x409/product")" = "AYN Thor (USB debugging)" ] || bad "the product name: $(cat "$G/strings/0x409/product")"
for f in ncm.usb0 acm.GS0 ffs.adb; do [ -d "$G/functions/$f" ] && [ -L "$G/configs/c.1/$f" ] || bad "function $f isn't in the configuration"; done
[ "$(cat "$G/UDC.asked")" = a600000.usb ] || bad "the controller: $(cat "$G/UDC.asked" 2>/dev/null)"
sh "$U" status | grep -q "^up:.*acm.GS0.*ncm.usb0" || bad "status: $(sh "$U" status)"
[ "$(sh "$U" up)" = "already up" ] || bad "a second up made it again"
rm -f "$G/UDC.asked" "$G"/id* "$G"/b* "$G"/strings/0x409/* "$G"/configs/c.1/MaxPower "$G"/configs/c.1/strings/0x409/*
sh "$U" down >/dev/null; [ -e "$G" ] && bad "down left the gadget: $(find "$G" | head -n 5 | tr '\n' ' ')"
# without adbd: no adb function, the other two there
sh "$U" up >/dev/null; [ -d "$G/functions/ffs.adb" ] && bad "an adb function with no adbd"
[ -d "$G/functions/ncm.usb0" ] && [ -d "$G/functions/acm.GS0" ] || bad "the network or the serial function is missing without adbd"
[ $fail = 0 ] && echo "usb-debug: all checks passed"
exit $fail

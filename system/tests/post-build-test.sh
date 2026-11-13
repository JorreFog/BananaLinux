#!/bin/sh
# buildroot/board/rgds/post-build.sh against a copy of the overlay in a temporary target folder, for the AYN Thor and
# for the RG DS: the board's hardware profile as /etc/banana/hw.conf (the others gone), the Thor's own systems list
# and Game Boy Advance core, the serial console's login only where there is a port, and what no image keeps
# (Buildroot's init scripts, the manuals). A board without a profile stops the build.
#   sh system/tests/post-build-test.sh
R=$(cd "$(dirname "$0")/../.." && pwd)
P=$R/buildroot/board/rgds/post-build.sh
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
fail=0
bad() { echo "FAIL post-build: $*"; fail=1; }
# target <board>: the overlay, with what Buildroot's packages would have left around it; then the script
target() {
    D=$T/$1; rm -rf "$D"; mkdir -p "$D"
    cp -a "$R/system/rootfs/." "$D/"
    mkdir -p "$D/usr/lib" "$D/usr/share/man/man1" "$D/usr/share/doc/x" "$D/usr/share/info" "$D/usr/libexec/bluetooth" "$D/var/lib/bluetooth"
    : > "$D/etc/init.d/S01syslogd"; : > "$D/etc/init.d/S40network"; : > "$D/usr/share/man/man1/x.1"
    printf '#!/bin/sh\n' > "$D/usr/libexec/bluetooth/bluetoothd"; chmod +x "$D/usr/libexec/bluetooth/bluetoothd"
    sh "$P" "$D" "$1" > "$T/log" 2>&1
}
same() { cmp -s "$1" "$2"; }
O=$R/system/rootfs/etc/banana
V=$(cat "$R/VERSION")

target thor || bad "thor: the script failed: $(cat "$T/log")"
B=$D/etc/banana
[ "$(cat "$B/board" 2>/dev/null)" = thor ] || bad "thor: etc/banana/board says '$(cat "$B/board" 2>/dev/null)'"
same "$B/hw.conf" "$O/hw/thor.conf" || bad "thor: hw.conf isn't the Thor's profile"
[ -e "$B/hw" ] && bad "thor: the other boards' profiles are still there"
same "$B/systems.conf" "$R/buildroot/board/thor/systems.conf" || bad "thor: systems.conf isn't the Thor's list"
[ -e "$B/systems.history" ] && bad "thor: the RG DS list's history is still there"
grep -q '^ttyS2::' "$D/etc/inittab" && bad "thor: a login on the serial port it doesn't have: $(grep '^ttyS2::' "$D/etc/inittab")"
grep -q '^::respawn:/usr/bin/banana-ui-session$' "$D/etc/inittab" || bad "thor: the menu's inittab line is gone"
[ "$(grep -c '^core\.gba = ' "$B/settings.conf")" = 1 ] && grep -qx 'core.gba = bananagba' "$B/settings.conf" || bad "thor: core.gba: $(grep '^core\.gba' "$B/settings.conf" | tr '\n' '|')"
# (nothing else of the settings template changed)
[ "$(diff "$O/settings.conf" "$B/settings.conf" | grep -c '^[<>]')" = 2 ] || bad "thor: more than core.gba changed in settings.conf"

target rgds || bad "rgds: the script failed: $(cat "$T/log")"
B=$D/etc/banana
[ "$(cat "$B/board" 2>/dev/null)" = rgds ] || bad "rgds: etc/banana/board says '$(cat "$B/board" 2>/dev/null)'"
same "$B/hw.conf" "$O/hw/rgds.conf" || bad "rgds: hw.conf isn't the RG DS's profile"
[ -e "$B/hw" ] && bad "rgds: the other boards' profiles are still there"
same "$B/systems.conf" "$O/systems.conf" || bad "rgds: systems.conf isn't the overlay's"
same "$B/systems.history" "$O/systems.history" || bad "rgds: the systems list's history is gone"
same "$B/settings.conf" "$O/settings.conf" || bad "rgds: settings.conf changed"
same "$D/etc/inittab" "$R/system/rootfs/etc/inittab" || bad "rgds: inittab changed (its serial login is ttyS2's)"
grep -q '^ttyS2::respawn:' "$D/etc/inittab" || bad "rgds: no login on the serial port"

# the Thor-only launchers: out of the other boards' images, kept in the Thor's
TL="run-wayland run-armsx2 run-azahar run-dolphin run-flycast run-ppsspp run-scummvm run-vita3k run-xemu ra-standalone run-browser run-pc pc-games steam usb-debug"
for f in $TL; do
    [ -e "$T/rgds/usr/libexec/banana/$f" ] && bad "rgds: the Thor-only $f is still there"
    [ -f "$R/system/rootfs/usr/libexec/banana/$f" ] || bad "the overlay has no $f (the list in post-build.sh is stale)"
    [ -f "$T/thor/usr/libexec/banana/$f" ] || bad "thor: $f is gone"
done
for f in run-retroarch settings-apply; do
    [ -e "$R/system/rootfs/usr/libexec/banana/$f" ] || continue
    [ -e "$T/rgds/usr/libexec/banana/$f" ] || bad "rgds: $f, every board's, is gone"
done

# every board's image
for b in thor rgds; do
    D=$T/$b
    [ "$(cat "$D/etc/banana/version" 2>/dev/null)" = "$V" ] || bad "$b: etc/banana/version"
    grep -qx "VERSION_ID=$V" "$D/usr/lib/os-release" && [ "$(readlink "$D/etc/os-release")" = ../usr/lib/os-release ] || bad "$b: os-release"
    [ "$(readlink "$D/roms")" = data/roms ] || bad "$b: /roms isn't the card's games"
    for d in data run boot; do [ -d "$D/$d" ] || bad "$b: no /$d"; done
    [ -z "$(ls "$D"/etc/init.d/S[0-9]* 2>/dev/null)" ] || bad "$b: Buildroot's init scripts are still there: $(cd "$D/etc/init.d" && echo S[0-9]*)"
    [ -f "$D/etc/init.d/rcS" ] || bad "$b: rcS is gone"
    for d in man doc info; do [ -e "$D/usr/share/$d" ] && bad "$b: usr/share/$d is still there"; done
    [ "$(readlink "$D/var/lib/bluetooth")" = /run/banana/bluetooth ] || bad "$b: BlueZ's pairings aren't kept in /run"
done

# a board with no profile: the build stops, and says which file it wanted
target nosuch && bad "a board without a hardware profile went through"
grep -q 'etc/banana/hw/nosuch.conf' "$T/log" || bad "no word on the missing profile: $(cat "$T/log")"
[ $fail = 0 ] && echo "post-build: all passed"
exit $fail

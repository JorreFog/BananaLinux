#!/bin/sh
# Panda3DS on an RG DS / RG DS Plus already running BananaOS, without building an image: run on the handheld (over
# SSH: Settings > System > SSH) from a copy of this folder and tools/3ds-bench on the card.
#
#   sh install-on-device.sh            puts everything in /data/panda3ds and the 3DS on the shelf
#   sh install-on-device.sh remove     takes the shelf entry and /data/panda3ds away again
#
# What it does: the core and panda3ds-bench (prebuilt for aarch64, see README.md) and the stress-test ROM in
# /data/panda3ds; a copy of run-retroarch there that loads the core from it (the image's /usr/lib/libretro is
# read-only); a [3ds] section in /data/banana/systems.conf (a copy of /etc/banana/systems.conf, which banana-ui reads
# instead when it exists), games in /data/roms/3ds. Restart the menu (or the handheld) to see the shelf.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
D=/data/panda3ds
SYS=/data/banana/systems.conf

if [ "$1" = remove ]; then
    [ -f $SYS ] && sed -i '/^# panda3ds-device begin/,/^# panda3ds-device end/d' $SYS
    rm -rf $D
    echo "removed $D and the [3ds] entry"
    exit 0
fi

unxz_to() {    # unxz_to <file.xz> <out>: xz, or busybox's xzcat
    if command -v xz >/dev/null; then xz -dc "$1" > "$2"; else busybox xzcat "$1" > "$2"; fi
}
mkdir -p $D /data/roms/3ds
unxz_to "$HERE/panda3ds-bench.xz" $D/panda3ds-bench
unxz_to "$HERE/panda3ds_libretro.so.xz" $D/panda3ds_libretro.so
chmod 755 $D/panda3ds-bench
ROM=$HERE/../3ds-bench/3ds-bench.3dsx
[ -f "$ROM" ] && cp "$ROM" $D/3ds-bench.3dsx
# run-retroarch with this core: the image's launcher, the core's path changed
sed 's|^CORE=/usr/lib/libretro/\$1_libretro.so$|CORE=/usr/lib/libretro/$1_libretro.so; [ "$1" = panda3ds ] \&\& CORE=/data/panda3ds/panda3ds_libretro.so|' \
    /usr/libexec/banana/run-retroarch > $D/run-retroarch
grep -q '/data/panda3ds/panda3ds_libretro.so' $D/run-retroarch || { echo "run-retroarch changed: its CORE= line wasn't found" >&2; exit 1; }
chmod 755 $D/run-retroarch

# the shelf: an image with the libretro-panda3ds package has [3ds] already
if ! grep -q '^\[3ds\]' /etc/banana/systems.conf; then
    [ -f $SYS ] || cp /etc/banana/systems.conf $SYS
    sed -i '/^# panda3ds-device begin/,/^# panda3ds-device end/d' $SYS
    cat >> $SYS <<CONF
# panda3ds-device begin
[3ds]
name = Nintendo 3DS
maker = Nintendo
ext = 3ds cci cxi 3dsx elf
run = $D/run-retroarch panda3ds %ROM%
perf = performance
always = yes
# panda3ds-device end
CONF
fi
echo "installed in $D; games go in /data/roms/3ds (decrypted .3ds/.cci/.cxi, or .3dsx homebrew)"
echo "benchmark: $D/panda3ds-bench $D/3ds-bench.3dsx --renderer gl"

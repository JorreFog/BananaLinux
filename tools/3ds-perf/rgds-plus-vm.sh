#!/bin/sh
# An RG DS Plus without the handheld, for Panda3DS's benchmark: QEMU's "virt" machine with what the RK3568 gives a
# game (4 Cortex-A55 cores, 1 GB of RAM), a kernel and a busybox initramfs holding panda3ds-bench, its libraries and
# the ROM. The guest runs the benchmark once, prints its table on the console and powers off.
#
#   tools/3ds-perf/rgds-plus-vm.sh <panda3ds-bench (aarch64)> <rom> [bench arguments...]
#
# Environment: KERNEL (an arm64 Image; default ~/vm/Image, from Ubuntu's linux-image-unsigned-*-generic:arm64),
# BUSYBOX (a static arm64 busybox), QEMU (qemu-system-aarch64), SYSROOT (the aarch64 libraries, default
# /usr/aarch64-linux-gnu + /usr/lib/aarch64-linux-gnu), PLUGIN (a QEMU plugin with its arguments, e.g.
# "$HOME/liba55prof.so,out=vm-prof.txt": counts the guest's instructions; no symbols in system mode).
#
# QEMU translates the A55's code on the PC, so the guest's clock says nothing about the handheld's speed: what this
# checks is that the build runs on an A55 (its instruction set, -mcpu=cortex-a55, the JIT's code) within 1 GB, and
# what its memory use is. Speed comes from instruction counts (a55prof, qemu-aarch64; see README.md).
set -e
BENCH=$1; ROM=$2
[ -f "$BENCH" ] && [ -f "$ROM" ] || { echo "usage: $0 <panda3ds-bench> <rom> [bench arguments...]" >&2; exit 2; }
shift 2
ARGS="$*"     # (before the library loop below reuses the positional parameters)
KERNEL=${KERNEL:-$HOME/vm/Image}
BUSYBOX=${BUSYBOX:-$HOME/vm/bb/usr/bin/busybox}
QEMU=${QEMU:-qemu-system-aarch64}
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
R=$WORK/root
mkdir -p "$R/bin" "$R/lib" "$R/usr/lib" "$R/proc" "$R/sys" "$R/dev" "$R/tmp" "$R/bench"
cp "$BUSYBOX" "$R/bin/busybox"
for c in sh mount poweroff cat free ls echo sync; do ln -s busybox "$R/bin/$c"; done
cp "$BENCH" "$R/bench/panda3ds-bench"
cp "$ROM" "$R/bench/"

# the bench's shared libraries, followed through their own NEEDED entries
LIBDIRS="/usr/aarch64-linux-gnu/lib /usr/lib/aarch64-linux-gnu ${SYSROOT:-}"
find_lib() { for d in $LIBDIRS; do [ -e "$d/$1" ] && { echo "$d/$1"; return; }; done; }
todo=$(aarch64-linux-gnu-readelf -d "$BENCH" | sed -n 's/.*Shared library: \[\(.*\)\]/\1/p')
seen=
while [ -n "$todo" ]; do
    set -- $todo; lib=$1; shift; todo="$*"
    case " $seen " in *" $lib "*) continue ;; esac
    seen="$seen $lib"
    path=$(find_lib "$lib"); [ -n "$path" ] || { echo "missing $lib" >&2; exit 1; }
    cp -L "$path" "$R/lib/$lib"
    todo="$todo $(aarch64-linux-gnu-readelf -d "$path" | sed -n 's/.*Shared library: \[\(.*\)\]/\1/p')"
done
cp -L "$(find_lib ld-linux-aarch64.so.1)" "$R/lib/"

cat > "$R/init" <<EOF
#!/bin/sh
mount -t proc proc /proc; mount -t sysfs sys /sys; mount -t devtmpfs dev /dev; mount -t tmpfs tmp /tmp
echo "== RG DS Plus VM: \$(grep -c ^processor /proc/cpuinfo) CPUs, \$(grep MemTotal /proc/meminfo)"
cd /bench
LD_LIBRARY_PATH=/lib ./panda3ds-bench "/bench/$(basename "$ROM")" $ARGS
echo "== bench exit \$?"
free -m
sync; poweroff -f
EOF
chmod +x "$R/init"
(cd "$R" && find . | cpio -o -H newc --quiet | gzip -1) > "$WORK/initramfs.gz"

PLUGIN_ARGS=
[ -n "$PLUGIN" ] && PLUGIN_ARGS="-plugin $PLUGIN"
"$QEMU" -M virt -cpu cortex-a55 -smp 4 -m 1024 -nographic -no-reboot $PLUGIN_ARGS \
    -kernel "$KERNEL" -initrd "$WORK/initramfs.gz" -append "console=ttyAMA0 quiet rdinit=/init panic=-1"

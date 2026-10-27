#!/bin/sh
# Boot a BananaOS card image on QEMU's arm64 "virt" machine with no window, and take a picture of the panel:
# the same boot tools/qemu-boot.sh does, for a machine with nobody at the screen (or for checking the menu from a
# script). The card is read exactly as the handheld's bootloader reads it: extlinux.conf's DEFAULT label, its
# LINUX file and its root=.
#
#   tools/qemu-headless.sh <banana-*.img[.gz]> [out dir] [seconds before the screenshot]
#
# One 1240x1080 panel, the Retroid Pocket Classic's screen (banana-ui's single-screen mode).
#
# KEEP=1 leaves the VM running afterwards, with its serial console on <out>/console.sock and its monitor on
# <out>/monitor.sock, so tools/qemu-console.py and tools/qemu-monitor.py can drive it (the pid is in <out>/qemu.pid).
#
# Writes, in <out dir> (qemu-headless by default): card.img, Image, serial.log, panel.png. Exit 0 when the boot
# reached the menu, 1 otherwise.
#
# No root needed: nothing is mounted; the boot partition is read with mtools (mcopy), which reads the FAT inside the
# image as a file. Needs qemu-system-aarch64, qemu-img, mtools, sfdisk, python3.
set -e
IMG=$1
[ -n "$IMG" ] && [ -f "$IMG" ] || { echo "usage: $0 <banana-*.img[.gz]> [out dir] [seconds]" >&2; exit 2; }
OUT=${2:-qemu-headless}
WAIT=${3:-170}
KEEP=${KEEP:-0}
APPEND_EXTRA=${APPEND_EXTRA:-}	# more of the kernel command line, e.g. banana.rotate=90 to try the panel's rotation
mkdir -p "$OUT"; OUT=$(cd "$OUT" && pwd)
CARD=$OUT/card.img
case $IMG in *.gz) gunzip -c "$IMG" > "$CARD" ;; *) cp "$IMG" "$CARD" ;; esac
qemu-img resize -q -f raw "$CARD" 2G

# the boot partition's offset, from the image's GPT (in bytes, for "@@offset" in mcopy)
BOOT_AT=$(sfdisk -J "$CARD" | python3 -c "import json,sys; print(json.load(sys.stdin)['partitiontable']['partitions'][0]['start']*512)")
mcopy -o -i "$CARD@@$BOOT_AT" ::/extlinux/extlinux.conf "$OUT/extlinux.conf"
LABEL=$(sed -n 's/^DEFAULT *//p' "$OUT/extlinux.conf" | head -n 1)
KERNEL=$(awk -v l="$LABEL" '$1 == "LABEL" { cur = $2 } $1 == "LINUX" && cur == l { print $2 }' "$OUT/extlinux.conf")
ROOT=$(awk -v l="$LABEL" '$1 == "LABEL" { cur = $2 } $1 == "APPEND" && cur == l { for (i = 2; i <= NF; i++) if ($i ~ /^root=/) print $i }' "$OUT/extlinux.conf")
[ -n "$KERNEL" ] && [ -n "$ROOT" ] || { echo "can't read the card's extlinux.conf" >&2; exit 1; }
mcopy -o -i "$CARD@@$BOOT_AT" "::$KERNEL" "$OUT/Image"
# the command line, for the virtual panel QEMU reports (the RPC's single 1240x1080 output)
GPU="virtio-gpu-pci,id=gpu,max_outputs=1,xres=1240,yres=1080"
APPEND="$ROOT rootfstype=squashfs ro rootwait quiet loglevel=3 console=ttyAMA0 video=Virtual-1:1240x1080 banana.console=1 banana.top=Virtual-1 banana.touch=virtio $APPEND_EXTRA"
echo "booting label $LABEL: $KERNEL, $ROOT"

SOCK=$OUT/monitor.sock
CONSOLE=$OUT/console.sock
rm -f "$SOCK" "$CONSOLE" "$OUT/serial.log"
# KEEP: the console on a socket with the traffic logged, for typing into it later
if [ "$KEEP" = 1 ]; then
    SERIAL="-chardev socket,id=con,path=$CONSOLE,server=on,wait=off,logfile=$OUT/serial.log -serial chardev:con"
else
    SERIAL="-serial file:$OUT/serial.log"
fi
qemu-system-aarch64 -M virt -cpu cortex-a55 -smp 4 -m 1024 -kernel "$OUT/Image" -append "$APPEND" \
    -device sdhci-pci -drive if=none,id=sd,file="$CARD",format=raw -device sd-card,drive=sd \
    -device "$GPU" -device virtio-keyboard-pci -device virtio-tablet-pci \
    -device virtio-net-pci,netdev=n0 -netdev user,id=n0 \
    -display none $SERIAL -monitor "unix:$SOCK,server=on,wait=off" \
    -no-reboot -rtc base=utc > "$OUT/qemu.out" 2>&1 &
PID=$!
echo $PID > "$OUT/qemu.pid"
trap 'kill $PID 2>/dev/null || true' EXIT INT TERM

echo "waiting ${WAIT}s for the first frame (serial log: $OUT/serial.log)"
i=0
while [ $i -lt "$WAIT" ]; do
    kill -0 $PID 2>/dev/null || { echo "QEMU exited early"; break; }
    sleep 5; i=$((i + 5))
    # the menu's own mark when it has drawn (banana-ui, on the console with banana.console=1)
    if grep -q 'banana-ui: the menu' "$OUT/serial.log" 2>/dev/null; then sleep 2; break; fi
done

if [ "$KEEP" = 1 ]; then
    echo "VM left running (pid $PID): console $CONSOLE, monitor $SOCK, serial log $OUT/serial.log"
    trap - EXIT INT TERM
    exit 0
fi

# the panel's head, as a picture
python3 - "$SOCK" "$OUT" <<'EOF'
import socket, sys, time
sock, out = sys.argv[1], sys.argv[2]
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
for _ in range(50):
    try: s.connect(sock); break
    except OSError: time.sleep(0.2)
else: sys.exit("the monitor socket never came up")
s.settimeout(20)
for head, name in ((0, "panel"),):
    s.sendall(f"screendump {out}/{name}.png -f png gpu {head}\n".encode())
    time.sleep(1.0)
    s.sendall(b"info status\n")
    time.sleep(0.3)
s.sendall(b"quit\n")
try: s.recv(65536)
except OSError: pass
EOF

echo "--- serial log (last 40 lines) ---"
tail -40 "$OUT/serial.log" 2>/dev/null || true
ls -l "$OUT"/panel.png "$OUT"/top.png "$OUT"/bottom.png 2>/dev/null || true
grep -q 'banana-ui' "$OUT/serial.log" 2>/dev/null && echo "the menu started" || echo "the menu did not start (see $OUT/serial.log)"

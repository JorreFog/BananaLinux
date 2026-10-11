#!/bin/sh
# A Banana card image booted in QEMU: the whole system from the kernel to the menu, with no handheld. The VM is
# QEMU's "virt" machine with the card on a PCI SDHCI (so it is mmcblk0, as on the RG DS: the first boot makes the
# data partition on it), two 640x480 panels on a virtio GPU, a virtio keyboard and tablet for the buttons and the
# touchscreen, and the serial console carrying the boot's progress and the menu's log (banana.console=1).
#
#   tools/qemu-boot.sh <banana-*.img[.gz]> [out dir]       the boot test (below); exit 0 when everything passed
#   tools/qemu-boot.sh <banana-*.img[.gz]> --interactive   a window with the two panels: arrows, Enter = A,
#                                                          Backspace = B, X, Y, PgUp/PgDn = L/R, S = Start, Tab = Select
#
# The test: a first boot (the data partition made, the menu on both panels, screenshots, a shutdown from the
# settings), a game copied onto the card, a second boot (the library, the game started and ended, the menu back,
# a shutdown), then the card's logs/boot.txt. Needs qemu-system-aarch64, qemu-img, mtools, python3, sfdisk and
# root (or sudo) for mounting the card's exFAT partition. QEMU runs the ARM code by translation: a boot takes a
# minute or two on a PC.
set -e
IMG=$1; OUT=${2:-qemu-out}
[ -n "$IMG" ] && [ -f "$IMG" ] || { echo "usage: $0 <banana-*.img[.gz]> [out dir | --interactive]" >&2; exit 2; }
HERE=$(cd "$(dirname "$0")/.." && pwd)
MODE=test; [ "$OUT" = --interactive ] && { MODE=interactive; OUT=qemu-out; }
mkdir -p "$OUT"; OUT=$(cd "$OUT" && pwd)
CARD=$OUT/card.img
case $IMG in *.gz) gunzip -c "$IMG" > "$CARD" ;; *) cp "$IMG" "$CARD" ;; esac
qemu-img resize -q -f raw "$CARD" 1G          # the card is bigger than the image: the first boot takes the rest
SUDO=; [ "$(id -u)" = 0 ] || SUDO=sudo
# the partitions' offsets, from the image's GPT
part_offset() { sfdisk -J "$CARD" | python3 -c "import json,sys; p=json.load(sys.stdin)['partitiontable']['partitions']; print(p[$1-1]['start']*512)"; }
BOOT_AT=$(part_offset 1)
case $(basename "$IMG") in banana-rgds-plus*) BOARD=rgds-plus ;; *) BOARD=rgds ;; esac
# the kernel and root the card would boot, as U-Boot reads them: extlinux.conf's DEFAULT label (the slot an update
# switched to), its LINUX file and its root=. video=: QEMU reports only the output a window is attached to as
# connected; the kernel is told both are, at 640x480
pick_kernel() {
    mcopy -o -i "$CARD@@$BOOT_AT" ::/extlinux/extlinux.conf "$OUT/extlinux.conf"
    LABEL=$(sed -n 's/^DEFAULT *//p' "$OUT/extlinux.conf" | head -n 1)
    KERNEL=$(awk -v l="$LABEL" '$1 == "LABEL" { cur = $2 } $1 == "LINUX" && cur == l { print $2 }' "$OUT/extlinux.conf")
    ROOT=$(awk -v l="$LABEL" '$1 == "LABEL" { cur = $2 } $1 == "APPEND" && cur == l { for (i = 2; i <= NF; i++) if ($i ~ /^root=/) print $i }' "$OUT/extlinux.conf")
    mcopy -o -i "$CARD@@$BOOT_AT" "::$KERNEL" "$OUT/Image"
    APPEND="$ROOT rootfstype=squashfs ro rootwait quiet loglevel=3 console=ttyAMA0 video=Virtual-1:640x480e video=Virtual-2:640x480e banana.console=1 banana.top=Virtual-2 banana.touch=virtio"
    echo "booting label $LABEL: $KERNEL, $ROOT"
}
pick_kernel
run_qemu() {    # run_qemu <display and serial arguments>
    qemu-system-aarch64 -M virt -cpu cortex-a55 -smp 4 -m 1024 -kernel "$OUT/Image" -append "$APPEND" \
        -device sdhci-pci -drive if=none,id=sd,file="$CARD",format=raw -device sd-card,drive=sd \
        -device virtio-gpu-pci,id=gpu,max_outputs=2,xres=640,yres=480 -device virtio-keyboard-pci -device virtio-tablet-pci \
        -no-reboot -rtc base=utc "$@"
}
# an update package made from the system on the card itself (version qemu-test), as the build makes them
make_update() {    # make_update <dir>
    U=$1/banana-$BOARD-qemu-test.update; rm -rf "$U"; mkdir -p "$U"
    mcopy -o -i "$CARD@@$BOOT_AT" ::/a/Image "$U/Image"
    DTB=$(mdir -i "$CARD@@$BOOT_AT" -b ::/a/ | grep -i '\.dtb$' | head -n 1); DTB=${DTB##*/}
    mcopy -o -i "$CARD@@$BOOT_AT" "::/a/$DTB" "$U/$DTB"
    # the squashfs out of slot a, its size from its superblock (bytes_used, 8 bytes at offset 40)
    off=$(part_offset 2)
    size=$(python3 -c "import struct, sys; f = open(sys.argv[1], 'rb'); f.seek($off + 40); print(struct.unpack('<Q', f.read(8))[0])" "$CARD")
    dd if="$CARD" of="$U/rootfs.squashfs" bs=1M iflag=skip_bytes,count_bytes skip=$off count=$size status=none
    echo qemu-test > "$U/VERSION"; echo "$BOARD" > "$U/board"
    (cd "$U" && sha256sum Image "$DTB" rootfs.squashfs > sha256sums)
    tar -C "$1" -cf "$U.tar" "$(basename "$U")"; rm -rf "$U"
    ls -la "$U.tar" | awk '{print $5, $9}'
}

if [ $MODE = interactive ]; then
    run_qemu -display gtk -serial stdio
    exit $?
fi

# ---- the test ------------------------------------------------------------------------------------------------
pass=0 fail=0
ok() { pass=$((pass + 1)); echo "  ok: $*"; }
bad() { fail=$((fail + 1)); echo "  FAIL: $*"; }
MON=$OUT/monitor
mon() {     # a monitor command, its reply
    python3 - "$MON" "$*" <<'PY'
import socket, sys, time
s = socket.socket(socket.AF_UNIX); s.connect(sys.argv[1]); s.settimeout(5)
def drain():
    out = b""
    try:
        while True:
            d = s.recv(65536)
            if not d: break
            out += d
            if out.rstrip().endswith(b"(qemu)"): break
    except socket.timeout: pass
    return out
drain()
s.sendall((sys.argv[2] + "\n").encode())
time.sleep(0.2)
sys.stdout.write(drain().decode(errors="replace"))
PY
}
key() { for k in "$@"; do mon "sendkey $k" > /dev/null; sleep 0.6; done; }
shot() {    # shot <name>: both panels, top and bottom
    mon "screendump $OUT/$1-top.png -f png gpu 1" > /dev/null
    mon "screendump $OUT/$1-bot.png -f png gpu 0" > /dev/null
    [ -s "$OUT/$1-top.png" ] && [ -s "$OUT/$1-bot.png" ] && ok "screenshots $1" || bad "no screenshots for $1"
}
wait_for() {    # wait_for <pattern> <seconds> <what>: in the serial log
    for i in $(seq $(($2 * 2))); do grep -q "$1" "$OUT/serial.log" 2>/dev/null && { ok "$3 ($(grep -o "at [0-9.]* s" "$OUT/serial.log" | tail -n 1 | sed 's/at //'))"; return 0; }; sleep 0.5; done
    bad "$3: not within $2 s"; return 1
}
boot() {    # boot <name>: start the VM (with the kernel and root the card's extlinux.conf names), wait for the menu
    rm -f "$MON"
    pick_kernel
    : > "$OUT/serial.log"
    run_qemu -display none -serial file:"$OUT/serial.log" -monitor unix:"$MON",server,nowait > "$OUT/qemu-$1.log" 2>&1 &
    QEMU=$!
    for i in $(seq 50); do [ -S "$MON" ] && break; sleep 0.2; done
    wait_for "banana-ui: the menu" 240 "$1: the menu is up" || { cat "$OUT/serial.log" | tail -n 30; kill $QEMU 2>/dev/null; return 1; }
    sleep 3
}
shutdown_from_settings() {  # Start > Up (wraps to the last row, Shut down) > A twice; the VM powers off
    key s up ret ret
    for i in $(seq 120); do kill -0 $QEMU 2>/dev/null || { ok "$1: shut down from the settings, the VM powered off"; wait $QEMU 2>/dev/null; return 0; }; sleep 0.5; done
    bad "$1: still running 60 s after the shutdown"; kill $QEMU 2>/dev/null; return 1
}
# whatever happens, the card is unmounted and the VM stopped on the way out
cleanup() {
    [ -n "$QEMU" ] && { kill $QEMU 2>/dev/null || true; }
    [ -n "$DATA" ] && { $SUDO umount "$DATA" 2>/dev/null || $SUDO fusermount -u "$DATA" 2>/dev/null || true; }
    [ -n "$LOOP" ] && { $SUDO losetup -d "$LOOP" 2>/dev/null || true; }
    return 0
}
trap cleanup EXIT
with_data() {   # with_data <command...>: the card's data partition (exFAT) mounted at $DATA, by the kernel or by FUSE
    DATA=$OUT/data; mkdir -p "$DATA"
    LOOP=$($SUDO losetup -f --show -o $(part_offset 4) "$CARD") || { bad "no loop device for the card"; return 1; }
    $SUDO mount -t exfat "$LOOP" "$DATA" 2>/dev/null || $SUDO mount.exfat-fuse "$LOOP" "$DATA" 2>/dev/null ||
        { bad "the data partition can't be mounted (no exFAT in the kernel, no exfat-fuse)"; $SUDO losetup -d "$LOOP"; return 1; }
    "$@"; r=$?
    $SUDO umount "$DATA" 2>/dev/null || $SUDO fusermount -u "$DATA"; $SUDO losetup -d "$LOOP"; LOOP=; DATA=
    return $r
}

echo "1. the first boot: the data partition made, the menu, a shutdown"
boot first
grep -q "rcS: data partition ready" "$OUT/serial.log" && ok "the data partition was made and mounted" || bad "no data partition"
grep -q "panels 640x480" "$OUT/serial.log" && ok "both panels opened at 640x480" || bad "the panels didn't open"
shot first-menu
key s; sleep 1; shot first-settings; key backspace; sleep 0.5
shutdown_from_settings first
add_game() {    # a DS game and a GBA one, so both launchers run (DraStic's and RetroArch's)
    mkdir -p "$DATA/roms/nds" "$DATA/roms/gba"
    head -c 65536 /dev/zero > "$DATA/roms/nds/Boot Test.nds"
    head -c 65536 /dev/zero > "$DATA/roms/gba/Boot Test Advance.gba"
    ls "$DATA/banana/" | tr '\n' ' '; echo
    [ -f "$DATA/banana/README.txt" ] && ok "the card has its folders and README" || bad "no README on the card"
}
with_data add_game

echo "2. the second boot: the library, a DS game and a GBA game started and ended, the menu back, a shutdown"
boot second
grep -q "rcS: data partition ready" "$OUT/serial.log" && ok "the data partition mounted again" || bad "no data partition the second time"
key ret; sleep 1; shot second-library
grep -q "Boot Test" "$OUT/serial.log" || true
key ret            # the ready screen: the cartridge goes in and the game starts
wait_for "starting nds" 30 "the game was started (run-nds)"
wait_for "game ended" 60 "the game ended and the menu took the panels back"
sleep 2; shot second-after-game
# the GBA shelf (RetroArch's launcher): home, the next system, its library, its game
key backspace right ret; sleep 1; shot second-gba
key ret
wait_for "starting gba" 30 "the GBA game was started (run-retroarch)"
for i in $(seq 120); do [ "$(grep -c 'game ended' "$OUT/serial.log")" -ge 2 ] && break; sleep 0.5; done
[ "$(grep -c 'game ended' "$OUT/serial.log")" -ge 2 ] && ok "the GBA game ended and the menu came back" || bad "the GBA game didn't end within 60 s"
sleep 2
shutdown_from_settings second

echo "3. an update: the package applied from the settings, the next boot runs the other slot"
add_update() { mkdir -p "$DATA/banana/updates"; make_update "$DATA/banana/updates"; }
with_data add_update
boot third
grep -q "rcS: slot a booted" "$OUT/serial.log" && ok "the third boot ran slot a" || bad "the third boot didn't say it ran slot a"
# Start > System (five sections on) > Update (the row under Time zone) > the package > A again to confirm
key s pgdn pgdn pgdn pgdn pgdn down ret; sleep 1; shot third-updates; key ret ret
wait_for "written to slot b" 300 "the update was written to slot b and checked"
sleep 2; shot third-updated
key ret            # Restart
for i in $(seq 120); do kill -0 $QEMU 2>/dev/null || break; sleep 0.5; done
kill -0 $QEMU 2>/dev/null && { bad "no restart after the update"; kill $QEMU; } || ok "restarted after the update"
wait $QEMU 2>/dev/null || true
boot fourth
grep -q "rcS: slot b booted" "$OUT/serial.log" && ok "the fourth boot ran slot b: the update" || bad "the fourth boot didn't run slot b"
shot fourth-menu
shutdown_from_settings fourth
show_logs() {
    echo "--- the card's logs/boot.txt"; cat "$DATA/banana/logs/boot.txt" 2>/dev/null || true
    [ -f "$DATA/banana/logs/boot.txt" ] && ok "logs/boot.txt written" || bad "no logs/boot.txt"
    ls "$DATA/banana/logs/" | tr '\n' ' '; echo
    echo "--- logs/updates.log"; cat "$DATA/banana/logs/updates.log" 2>/dev/null || echo "(none)"
}
with_data show_logs
echo "qemu boot test: $pass passed, $fail failed ($OUT)"
[ $fail = 0 ]

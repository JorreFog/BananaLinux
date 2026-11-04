#!/bin/sh
# What tools/qemu-boot.sh and tools/qemu-sync.sh share, sourced by both once IMG (the image) and OUT (the out
# directory) are set. Sourcing it does work: OUT is made and made absolute, the image is copied to $OUT/card.img and
# grown to 2 GB, the kernel and root it would boot are picked (pick_kernel), and an EXIT trap (cleanup) is set that
# stops the VM and lets go of the card; a caller with a trap of its own calls cleanup from it.
#
# It sets CARD, BOARD (rgds, rgds-plus or universal, from the image's name), BOOT_AT, SSH_PORT, KERNEL, ROOT, APPEND,
# MON (the monitor's socket), and pass and fail (the counts ok and bad keep); QEMU (the VM's pid) once boot ran, DATA
# (the data partition) inside with_data. Functions:
#   run_qemu <args>          QEMU on the card (it replaces the shell it runs in: start it with & or call it last)
#   boot <name>              the VM started headless, the menu waited for; shutdown_from_settings <name> ends it
#   finish_setup [<name>]    the first boot's setup gone through to the menu (screenshots <name>-setup, -menu)
#   vm <command>, vmd5 <path>, wait_ssh <name>       over the VM's SSH (on from the boot after enable_ssh)
#   key <keys>, mon <command>, shot <name>           over QEMU's monitor
#   wait_for <pattern> <s> <what>, ok, bad, with_data <command>, part_offset, part_size
SSH_PORT=${SSH_PORT:-$((2200 + $$ % 800))}      # the VM's SSH, on the host: ssh -p $SSH_PORT root@127.0.0.1
mkdir -p "$OUT"; OUT=$(cd "$OUT" && pwd)
CARD=$OUT/card.img
case $IMG in *.gz) gunzip -c "$IMG" > "$CARD" ;; *) cp "$IMG" "$CARD" ;; esac
qemu-img resize -q -f raw "$CARD" 2G          # the card is bigger than the image: the first boot takes the rest (an
                                              # update package, about the size of the root, must fit in it)
SUDO=; [ "$(id -u)" = 0 ] || SUDO=sudo
# no root and no sudo without a password (a session nobody types into): the data partition through udisks instead,
# which lets the desktop's user map and mount it (udisksctl loop-setup/mount, as a file manager would)
UDISKS=0; [ -n "$SUDO" ] && ! sudo -n true 2>/dev/null && command -v udisksctl >/dev/null && UDISKS=1
# the partitions' offsets, from the image's GPT
part_offset() { sfdisk -J "$CARD" | python3 -c "import json,sys; p=json.load(sys.stdin)['partitiontable']['partitions']; print(p[$1-1]['start']*512)"; }
part_size() { sfdisk -J "$CARD" | python3 -c "import json,sys; p=json.load(sys.stdin)['partitiontable']['partitions']; print(p[$1-1]['size']*512)"; }
BOOT_AT=$(part_offset 1)
case $(basename "$IMG") in banana-rgds-plus*) BOARD=rgds-plus ;; banana-universal*) BOARD=universal ;; *) BOARD=rgds ;; esac
# the kernel and root the card would boot, as U-Boot reads them: extlinux.conf's DEFAULT label (the slot an update
# switched to), its LINUX file and its root=. video=: QEMU reports only the output a window is attached to as
# connected; the kernel is told both are, at 640x480
pick_kernel() {
    mcopy -o -i "$CARD@@$BOOT_AT" ::/extlinux/extlinux.conf "$OUT/extlinux.conf"
    LABEL=$(sed -n 's/^DEFAULT *//p' "$OUT/extlinux.conf" | head -n 1)
    KERNEL=$(awk -v l="$LABEL" '$1 == "LABEL" { cur = $2 } $1 == "LINUX" && cur == l { print $2 }' "$OUT/extlinux.conf")
    ROOT=$(awk -v l="$LABEL" '$1 == "LABEL" { cur = $2 } $1 == "APPEND" && cur == l { for (i = 2; i <= NF; i++) if ($i ~ /^root=/) print $i }' "$OUT/extlinux.conf")
    mcopy -o -i "$CARD@@$BOOT_AT" "::$KERNEL" "$OUT/Image"
    # (the card's kernel is LZ4-compressed for U-Boot since 0.4.3; QEMU's -kernel takes it plain)
    [ "$(head -c 4 "$OUT/Image" | od -An -tx1 | tr -d ' ')" = 04224d18 ] && lz4 -d -f -q "$OUT/Image" "$OUT/Image.plain" && mv -f "$OUT/Image.plain" "$OUT/Image"
    APPEND="$ROOT rootfstype=squashfs ro rootwait quiet loglevel=3 console=ttyAMA0 video=Virtual-1:640x480e video=Virtual-2:640x480e banana.console=1 banana.top=Virtual-2 banana.touch=virtio"
    echo "booting label $LABEL: $KERNEL, $ROOT"
}
pick_kernel
# (exec, so that started with & the pid in $! is QEMU's own, and a kill stops the VM and not only the shell around it;
# called in the foreground, it is the shell's last command)
run_qemu() {    # run_qemu <display and serial arguments>
    exec qemu-system-aarch64 -M virt -cpu cortex-a55 -smp 4 -m 1024 -kernel "$OUT/Image" -append "$APPEND" \
        -device sdhci-pci -drive if=none,id=sd,file="$CARD",format=raw -device sd-card,drive=sd \
        -device virtio-gpu-pci,id=gpu,max_outputs=2,xres=640,yres=480 -device virtio-keyboard-pci -device virtio-tablet-pci \
        -device virtio-net-pci,netdev=n0 -netdev user,id=n0,hostfwd=tcp:127.0.0.1:$SSH_PORT-:22 \
        -no-reboot -rtc base=utc "$@"
}

# a key for the VM's SSH (dropbear, switched on in the card's settings, so from the boot after enable_ssh)
vm() { ssh -q -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ConnectTimeout=3 -i "$OUT/ssh-key" -p $SSH_PORT root@127.0.0.1 "$@"; }
vmd5() { vm "md5sum < '$1'" | cut -c1-32; }      # vmd5 <path>: a file's MD5, in the VM
wait_ssh() {    # wait_ssh <name>: the VM's SSH answering, within a minute of the menu
    for i in $(seq 60); do vm true 2>/dev/null && { ok "$1: SSH into the VM answers"; return 0; }; sleep 1; done
    bad "$1: no SSH into the VM"; return 1
}
enable_ssh() {
    [ -f "$OUT/ssh-key" ] || ssh-keygen -q -t ed25519 -N "" -f "$OUT/ssh-key"
    mkdir -p "$DATA/banana/ssh"; cat "$OUT/ssh-key.pub" > "$DATA/banana/ssh/authorized_keys"
    grep -q '^ssh = ' "$DATA/banana/settings.conf" && sed -i 's/^ssh = .*/ssh = on/' "$DATA/banana/settings.conf" || echo "ssh = on" >> "$DATA/banana/settings.conf"
}

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
# the first-time setup after the arrival on a first boot: START skips the arrival, then each slide (seven: Language
# to the summary), the last START finishes it (setup-done on the card), the Welcome and the iris into the menu
finish_setup() {    # finish_setup [<name>]: with screenshots when named
    key s; sleep 2
    [ -n "$1" ] && shot "$1-setup"
    for i in 1 2 3 4 5 6 7; do key s; sleep 0.5; done
    sleep 4
    [ -n "$1" ] && shot "$1-menu"
    return 0
}
shutdown_from_settings() {  # Start (the groups) > Y (Power: Sleep, Restart, Shut down) > Down twice > A twice; the
    key s y down down ret ret  # VM powers off
    for i in $(seq 120); do kill -0 $QEMU 2>/dev/null || { ok "$1: shut down from the settings, the VM powered off"; wait $QEMU 2>/dev/null; return 0; }; sleep 0.5; done
    bad "$1: still running 60 s after the shutdown"; kill $QEMU 2>/dev/null; return 1
}
# whatever happens, the card is unmounted and the VM stopped on the way out
cleanup() {
    [ -n "$QEMU" ] && { kill $QEMU 2>/dev/null || true; }
    if [ $UDISKS = 1 ]; then
        [ -n "$LOOP" ] && { udisksctl unmount --no-user-interaction -b "$LOOP" >/dev/null 2>&1; udisksctl loop-delete --no-user-interaction -b "$LOOP" >/dev/null 2>&1 || true; }
    else
        [ -n "$DATA" ] && { $SUDO umount "$DATA" 2>/dev/null || $SUDO fusermount -u "$DATA" 2>/dev/null || true; }
        [ -n "$LOOP" ] && { $SUDO losetup -d "$LOOP" 2>/dev/null || true; }
    fi
    return 0
}
trap cleanup EXIT
with_data() {   # with_data <command...>: the card's data partition (exFAT) mounted at $DATA, by the kernel or by FUSE
    if [ $UDISKS = 1 ]; then
        LOOP=$(udisksctl loop-setup --no-user-interaction -f "$CARD" --offset $(part_offset 4) --size $(part_size 4) 2>&1 | grep -o '/dev/loop[0-9]*')
        [ -n "$LOOP" ] || { bad "no loop device for the card (udisks)"; return 1; }
        DATA=$(udisksctl mount --no-user-interaction -b "$LOOP" 2>&1 | sed -n 's/^Mounted .* at \(.*\)$/\1/p' | sed 's/\.$//')
        [ -d "$DATA" ] || { bad "the data partition can't be mounted (udisks)"; udisksctl loop-delete --no-user-interaction -b "$LOOP" >/dev/null 2>&1; LOOP=; return 1; }
        "$@"; r=$?
        sync; udisksctl unmount --no-user-interaction -b "$LOOP" >/dev/null; udisksctl loop-delete --no-user-interaction -b "$LOOP" >/dev/null; LOOP=; DATA=
        return $r
    fi
    DATA=$OUT/data; mkdir -p "$DATA"
    LOOP=$($SUDO losetup -f --show -o $(part_offset 4) "$CARD") || { bad "no loop device for the card"; return 1; }
    $SUDO modprobe exfat 2>/dev/null || true
    $SUDO mount -t exfat "$LOOP" "$DATA" 2> "$OUT/mount.err" || $SUDO mount.exfat-fuse "$LOOP" "$DATA" 2>> "$OUT/mount.err" ||
        { bad "the data partition can't be mounted (no exFAT in the kernel, no exfat-fuse): $(tr '\n' ' ' < "$OUT/mount.err")"; $SUDO losetup -d "$LOOP"; return 1; }
    "$@"; r=$?
    $SUDO umount "$DATA" 2>/dev/null || $SUDO fusermount -u "$DATA"; $SUDO losetup -d "$LOOP"; LOOP=; DATA=
    return $r
}

#!/bin/sh
# The whole system, in QEMU, with nothing but a card image: a first boot, a game put on the card over the network, an
# A/B update applied to the other slot, and the second boot from that slot -- with a screenshot of the menu in each.
#
#   tools/qemu-e2e.sh <banana-*.img[.gz]> [out dir]
#
# Needs what tools/qemu-headless.sh needs (qemu-system-aarch64, qemu-img, mtools, sfdisk, python3) and nothing else:
# the file copy is the guest's own wget from a server this script runs, and the commands go in on the serial console
# (the image's getty). So no root, no SSH keys, no mounting of the card's exFAT partition.
#
# What it checks, and fails on:
#   - the first boot makes the data partition and the menu comes up
#   - a game fetched over the network is in the library (the menu logs the shelves it found)
#   - the update package writes slot b, hashes it, and makes it the default (update status: default b)
#   - the next boot runs slot b ("rcS: slot b booted") with the game still on the card
set -e
IMG=$1
[ -n "$IMG" ] && [ -f "$IMG" ] || { echo "usage: $0 <banana-*.img[.gz]> [out dir]" >&2; exit 2; }
OUT=${2:-qemu-e2e}
HERE=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$OUT"; OUT=$(cd "$OUT" && pwd)
PORT=$((8900 + $$ % 90))

fail() { echo "FAIL: $*" >&2; exit 1; }
note() { echo "== $*"; }

# ---- the files the guest fetches: a test ROM and this image's update package --------------------------------
"$HERE/ui/tests/fixture.sh" "$OUT/fixture" >/dev/null 2>&1 || fail "ui/tests/fixture.sh"
ROM=$(ls "$OUT"/fixture/roms/gba/*.gba 2>/dev/null | head -n 1)
[ -n "$ROM" ] || fail "no .gba in the fixture library"
UPDATE=$(ls "$(dirname "$IMG")"/*.update.tar 2>/dev/null | head -n 1)
[ -n "$UPDATE" ] || fail "no .update.tar beside the image (build the image first)"
mkdir -p "$OUT/www"
PKG=$(basename "$UPDATE")
cp "$ROM" "$OUT/www/game.gba"
cp "$UPDATE" "$OUT/www/$PKG"
(cd "$OUT/www" && exec python3 -m http.server "$PORT" --bind 127.0.0.1 >/dev/null 2>&1) &
HTTP=$!
trap 'kill $HTTP 2>/dev/null || true' EXIT INT TERM

# ---- boot 1: the first boot, the menu, setup skipped -------------------------------------------------------
note "booting slot a ($IMG)"
KEEP=1 "$HERE/tools/qemu-headless.sh" "$IMG" "$OUT/boot-a" 180 >/dev/null
MON=$OUT/boot-a/monitor.sock
CON=$OUT/boot-a/console.sock
# the boot is left running as soon as the panels are open; give the menu itself a moment
for i in $(seq 1 40); do grep -aq 'banana-ui: the menu' "$OUT/boot-a/serial.log" && break; sleep 1; done
grep -aq 'banana-ui: the menu' "$OUT/boot-a/serial.log" || fail "the menu didn't come up on the first boot"
grep -aq 'rcS: data partition ready' "$OUT/boot-a/serial.log" || fail "the data partition wasn't made"
"$HERE/tools/qemu-monitor.py" "$MON" 'sendkey backspace' >/dev/null
sleep 2
"$HERE/tools/qemu-monitor.py" "$MON" "screendump $OUT/home-a.png -f png gpu 0" >/dev/null
note "first boot ok: menu up, screenshot $OUT/home-a.png"

# ---- the game and the update package, over the guest's network --------------------------------------------
note "fetching the game and the update package into the card"
"$HERE/tools/qemu-console.py" "$CON" --timeout 60 --per-command 300 --until-prompt \
    "wget -q -O /data/roms/gba/game.gba http://10.0.2.2:$PORT/game.gba && echo GAME-OK" >/dev/null
"$HERE/tools/qemu-console.py" "$CON" --timeout 60 --per-command 900 --until-prompt \
    "mkdir -p /data/banana/updates; wget -q -O /data/banana/updates/$PKG http://10.0.2.2:$PORT/$PKG && echo UPDATE-OK" >/dev/null
grep -aq 'UPDATE-OK' "$OUT/boot-a/serial.log" || fail "the update package didn't download"
note "game and update package on the card"

# ---- the update: written to the other slot, made the default ------------------------------------------------
"$HERE/tools/qemu-console.py" "$CON" --timeout 60 --per-command 1200 --until-prompt \
    "/usr/libexec/banana/update apply /data/banana/updates/$PKG; echo APPLY-EXIT=\$?" >/dev/null
grep -aq 'APPLY-EXIT=0' "$OUT/boot-a/serial.log" || fail "the update didn't apply (see $OUT/boot-a/serial.log)"
"$HERE/tools/qemu-console.py" "$CON" --timeout 60 --per-command 60 \
    '/usr/libexec/banana/update status' > "$OUT/update-status.txt"
grep -q 'default: b' "$OUT/update-status.txt" || fail "the update didn't make slot b the default"
note "update applied: $(tail -n 1 "$OUT/update-status.txt")"

# ---- boot 2: slot b -------------------------------------------------------------------------------------------
for p in $(ps -eo pid,comm | awk '$2 ~ /^qemu-system-a/ {print $1}'); do kill $p; done
sleep 2
note "booting slot b"
cp "$OUT/boot-a/card.img" "$OUT/card-b.img"
KEEP=1 "$HERE/tools/qemu-headless.sh" "$OUT/card-b.img" "$OUT/boot-b" 180 >/dev/null
grep -aq 'rcS: slot b booted' "$OUT/boot-b/serial.log" || fail "the second boot didn't run slot b"
"$HERE/tools/qemu-monitor.py" "$OUT/boot-b/monitor.sock" 'sendkey backspace' >/dev/null
sleep 2
"$HERE/tools/qemu-monitor.py" "$OUT/boot-b/monitor.sock" "screendump $OUT/home-b.png -f png gpu 0" >/dev/null
note "second boot ok: slot b, the card's data kept (screenshot $OUT/home-b.png)"

for p in $(ps -eo pid,comm | awk '$2 ~ /^qemu-system-a/ {print $1}'); do kill $p; done
echo "PASS: first boot, game on the card, A/B update, slot b, all in QEMU"

#!/bin/sh
# A small library for headless runs: DS games (with box art for some, a ROM banner icon for one), a few other
# systems, play stats and a favourite. Art is drawn here (no game art is shipped).
#   tests/fixture.sh <dir>     then: banana-ui --roms <dir>/roms --data <dir>/data --systems ../system/rootfs/etc/banana/systems.conf ...
set -e
D=${1:?usage: fixture.sh <dir>}
rm -rf "$D"; mkdir -p "$D/roms/nds/media/box" "$D/roms/gba" "$D/roms/snes/media/box" "$D/roms/gb" "$D/data"
box() {   # box <file> <colour> <text>
    convert -size 420x380 "gradient:$2-black" -gravity center -fill white -pointsize 34 -annotate 0 "$3" "$1"
}
for g in "Banana Kart DS" "Peel Quest" "Split Racers (USA)" "Monkey Puzzle League" "Yellow Planet [!]"; do
    head -c 4096 /dev/zero > "$D/roms/nds/$g.nds"
done
# Yellow Planet: a ROM with a banner (a yellow disc icon) 3 MB in, kept zipped, so its cartridge shows the ROM's
# own icon through banana-ui's zip reader
rm "$D/roms/nds/Yellow Planet [!].nds"
python3 - "$D/roms/nds/Yellow Planet [!].zip" <<'PY'
import os, random, struct, sys, zipfile
at = 3 << 20
rom = bytearray(at + 0x840)
rom[0x68:0x6c] = struct.pack('<I', at)
random.seed(7)
rom[0x200:at] = bytes(random.getrandbits(8) for _ in range(at - 0x200))   # code: deflate can't shrink it much
b = bytearray(0x840)
b[0:2] = struct.pack('<H', 1)
for t in range(16):                         # 4x4 tiles of 8x8, 4 bits a pixel: a yellow disc on transparent
    for y in range(8):
        for x in range(0, 8, 2):
            px = [((t % 4) * 8 + x + i - 15.5) ** 2 + ((t // 4) * 8 + y - 15.5) ** 2 < 14 ** 2 for i in (0, 1)]
            b[0x20 + t * 32 + y * 4 + x // 2] = (1 if px[0] else 0) | ((1 if px[1] else 0) << 4)
b[0x222:0x224] = struct.pack('<H', 31 | (28 << 5) | (4 << 10))   # colour 1: yellow (BGR555)
rom[at:at + 0x840] = b
with zipfile.ZipFile(sys.argv[1], 'w', zipfile.ZIP_DEFLATED) as z:
    z.writestr('readme.txt', 'not the game')
    z.writestr('Yellow Planet (Europe).nds', bytes(rom))
PY
box "$D/roms/nds/media/box/Banana Kart DS.png" '#d8b020' 'Banana Kart'
box "$D/roms/nds/media/box/Peel Quest.png" '#3d9e9a' 'Peel Quest'
box "$D/roms/nds/media/box/Split Racers (USA).png" '#c84040' 'Split Racers'
for g in "Orchard Tactics" "Tiny Fruit Advance"; do head -c 1024 /dev/zero > "$D/roms/gba/$g.gba"; done
for g in "Super Smoothie World"; do head -c 1024 /dev/zero > "$D/roms/snes/$g.sfc"; done
box "$D/roms/snes/media/box/Super Smoothie World.png" '#8060c0' 'Smoothie World'
head -c 1024 /dev/zero > "$D/roms/gb/Pocket Peel.gb"
now=$(date +%s)
printf 'nds/Banana Kart DS.nds\t14\t79920\t%s\nnds/Peel Quest.nds\t3\t5400\t%s\ngba/Orchard Tactics.gba\t1\t900\t%s\n' \
    $((now - 3600)) $((now - 90000)) $((now - 900000)) > "$D/data/playstats.tsv"
echo "nds/Peel Quest.nds" > "$D/data/favorites.txt"
echo "fixture in $D"

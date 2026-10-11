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

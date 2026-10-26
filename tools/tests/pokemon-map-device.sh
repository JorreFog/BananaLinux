#!/bin/sh
# pokemon-map-device.sh (on a handheld, after a Pokémon game started with Settings > Other systems > Pokémon
# companion on): the region map pictures banana-ui decoded from the card's ROM, against the checksums recorded for
# the official releases (FNV-1a of each picture's RGBA, as pokemon-test's POKE_MAPDUMP prints them). The ROMs are the
# player's; no game data is in Banana, only these numbers.
#   pokemon-map-device.sh [banana-ui log]
LOG=${1:-/data/banana/logs/banana-ui.log}
line=$(grep -a "region map pictures" "$LOG" | tail -n 1)
[ -n "$line" ] || { echo "no 'region map pictures' line in $LOG"; exit 2; }
echo "$line"
sums=${line#*;}
sums=$(echo $sums)
case "$sums" in
"a2411930") echo "OK: Hoenn (Ruby, Sapphire, Emerald: one picture, the same in all three)" ;;
"a4bf80f3 0172bbf0 da4fcbd0 c4898a26") echo "OK: Kanto, Sevii 1-3, 4-5, 6-7 (FireRed, LeafGreen)" ;;
"4793785e") echo "OK: Pokemon Unbound 2.1.1.1's Borrius map" ;;
"") echo "FAIL: no picture (the squares are drawn)"; exit 1 ;;
*) echo "UNKNOWN: pictures not recorded here (another release or a hack): look at the Map page"; exit 3 ;;
esac

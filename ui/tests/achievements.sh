#!/bin/sh
# The Achievements page (X on a game > Achievements) and the game panel's list (its List button), headless at both
# panel sizes, against made-up RetroAchievements replies (BANANA_RA_FIXTURE: achievements.h): a list of 120 with
# some unlocked (softcore and hardcore, two with the times the game panel kept), long names and descriptions, badges
# and locked badges; then the same game offline (the card's cache, "as of"), no account, a system without
# RetroAchievements, a game the server doesn't know, and the list in the game panel from the cache. Every request
# made is checked: read-only, never a session started. The PNGs are left in $OUT/ach for a look.
set -e
HERE=$(cd "$(dirname "$0")/.." && pwd)
OUT=${OUT:-$HERE/tests/out}/ach
rm -rf "$OUT"; mkdir -p "$OUT"
fail=0
FX=$OUT/fx
"$HERE/tests/fixture.sh" "$FX" >/dev/null
printf 'ra_user=good\nra_retroarch_token=tok\n' > "$FX/data/accounts.conf"
ROM="$FX/roms/gba/Orchard Tactics.gba"
# the server's replies: the game, its list (one unofficial, which isn't shown), the account's unlocks
RF=$OUT/ra; RF0=$OUT/ra-unknown; NONE=$OUT/ra-down
mkdir -p "$RF/badges" "$RF0" "$NONE"
python3 -I - "$RF" "$RF0" <<'PY'
import json, sys
rf, rf0 = sys.argv[1], sys.argv[2]
json.dump({"Success": True, "GameID": 5501}, open(rf + "/gameid.json", "w"))
json.dump({"Success": True, "GameID": 0}, open(rf0 + "/gameid.json", "w"))
pts = [1, 2, 3, 5, 10, 25, 50, 100]
achs = []
for i in range(120):
    t = "Orchard step %d" % (i + 1)
    d = "Pick %d apples before the frost" % ((i + 1) * 3)
    if i == 1: t = "A remarkably long achievement title that cannot possibly fit on one row of the list"
    if i == 2: d = ("Clear every orchard in the valley without letting a single crow take a fruit, then return to the "
                    "farmhouse before nightfall and talk to the old gardener, who has been waiting by the gate since the "
                    "first harvest of the year. Only the patient will see the ending.")
    if i == 4: t, d = "Café à la pomme", "Une description en français"
    achs.append({"ID": 1001 + i, "MemAddr": "0xH%04x=1" % i, "Title": t, "Description": d, "Points": pts[i % 8],
                 "Author": "x", "BadgeName": "b%04d" % (i + 1), "Flags": 3})
achs.insert(3, {"ID": 9999, "MemAddr": "", "Title": "Unofficial one", "Description": "", "Points": 5, "BadgeName": "b9999", "Flags": 5})
json.dump({"Success": True, "PatchData": {"ID": 5501, "Title": "Orchard Tactics", "ConsoleID": 5, "Achievements": achs}},
          open(rf + "/patch.json", "w"))
json.dump({"Success": True, "UserUnlocks": list(range(1001, 1013)) + [1020], "GameID": 5501, "HardcoreMode": False}, open(rf + "/unlocks-h0.json", "w"))
json.dump({"Success": True, "UserUnlocks": [1001, 1002, 1003, 1004], "GameID": 5501, "HardcoreMode": True}, open(rf + "/unlocks-h1.json", "w"))
PY
for i in 1 2 3 4 5 6 7 8; do convert -size 64x64 "xc:#e0a020" -fill "#7a3010" -draw "circle 32,32 32,12" "$RF/badges/b000$i.png" 2>/dev/null; done
for i in 13 14 15 16 17; do convert -size 64x64 "xc:#505058" -fill "#808088" -draw "circle 32,32 32,12" "$RF/badges/b00${i}_lock.png" 2>/dev/null; done
# the times the game panel kept for two of them (its session has them; r=unlocks hasn't)
mkdir -p "$FX/data/cache/ra"
printf 'banana-achievements 1\ngame\t5501\t1761500000\tOrchard Tactics\na\t1001\t1\t1761000000\t1762000000\tb0001\tOld\t\na\t1005\t10\t1761500000\t0\tb0005\tOld\t\n' > "$FX/data/cache/ra/game-5501.tsv"
cp -r "$FX/data" "$OUT/noacct-data"; rm "$OUT/noacct-data/accounts.conf"
run()	# run <name> <data> <fixture> <script>
{
    n=$1 d=$2 f=$3; shift 3
    : > "$f/requests.log"
    BANANA_RA_FIXTURE="$f" "$HERE/banana-ui" --headless "$OUT/$size-" --size $size --assets "$HERE/assets/rnds" \
        --systems "$HERE/../system/rootfs/etc/banana/systems.conf" --roms "$FX/roms" --data "$d" --script "$*" 2> "$OUT/$size-$n.log" ||
        { echo "FAIL $size $n: banana-ui exited $?"; fail=1; }
    cp "$f/requests.log" "$OUT/$size-$n.requests"
}
check() { grep -q "$2" "$OUT/$size-$1.log" || { echo "FAIL $size $1: no '$2'"; fail=1; }; }
asked() { grep -q "$2" "$OUT/$size-$1.requests" || { echo "FAIL $size $1: no request '$2'"; fail=1; }; }
for size in 640x480 1024x768; do
    rm -f "$FX/data/cache/ra/ids.tsv"
    # the library: X on Orchard Tactics, its game page, Achievements (the first row of a game that isn't a DS game)
    run list "$FX/data" "$RF" "homeid gba; press a; wait 2400; press x; shot ach-gamepage; press a; achdone; shot ach-list0; achdone; shot ach-list; \
        press down; press down; achdone; shot ach-long; press down; press down; press down; press down; press down; press down; press down; press down; press down; press down; press down; press down; achdone; shot ach-locked; \
        press r; press r; press r; press r; press r; press r; press r; press r; press r; press r; press r; press r; press r; press r; press r; press r; press r; press r; press r; press r; press r; press r; press r; press r; press r; shot ach-end; \
        press b; shot ach-back"
    check list "press x: game page"
    check list "achdone: ready 120 13 online"
    check list "press b: settings result"
    asked list "r=gameid g= m=[0-9a-f]\{32\}"
    asked list "r=patch g=5501"
    asked list "r=unlocks g=5501 m= h=0"
    asked list "r=unlocks g=5501 m= h=1"
    grep -q "startsession\|r=login2\|r=awardachievement" "$OUT/$size-list.requests" && { echo "FAIL $size list: not read-only: $(cat "$OUT/$size-list.requests")"; fail=1; }
    grep -q "Orchard Tactics.gba" "$FX/data/cache/ra/ids.tsv" || { echo "FAIL $size list: the game's id wasn't kept"; fail=1; }
    grep -q "^a	1001	1	1761000000	1762000000	" "$FX/data/cache/ra/game-5501.tsv" || { echo "FAIL $size list: the game panel's times weren't kept"; fail=1; }
    # the server down: the list as the card kept it, "as of"; no account; a system without RetroAchievements; a game the
    # server doesn't know
    run offline "$FX/data" "$NONE" "achopen gba $ROM; achdone; shot ach-offline"
    check offline "achdone: ready 120 13 offline"
    grep -q "r=gameid" "$OUT/$size-offline.requests" && { echo "FAIL $size offline: identified again"; fail=1; }
    run noacct "$OUT/noacct-data" "$RF" "achopen gba $ROM; achdone; shot ach-noacct"
    check noacct "achdone: no-account"
    [ -s "$OUT/$size-noacct.requests" ] && { echo "FAIL $size noacct: asked the server without an account"; fail=1; }
    run nosys "$FX/data" "$RF" "achopen pico-8 $FX/roms/gb/Pocket Peel.gb; achdone; shot ach-nosys"
    check nosys "achdone: no-system"
    run nogame "$FX/data" "$RF0" "achopen gb $FX/roms/gb/Pocket Peel.gb; achdone; shot ach-nogame"
    check nogame "achdone: no-game"
    # the game panel: its List button on the achievements box, the list from the cache (no session from RetroArch here),
    # its second page, Back
    run panel "$FX/data" "$NONE" "home 0; gamesys gba $ROM; gamepanel 1 $ROM; gshot gach-box; gtouch 560 100; gshot gach-list; gtouch 568 392; gshot gach-p2; gtouch 320 446; gshot gach-back"
    check panel "gtouch 560 100: action 0 keys 0 view achievements"
    check panel "gtouch 320 446: action 0 keys 0 view $"
    for s in ach-gamepage ach-list ach-long ach-locked ach-end ach-offline ach-noacct ach-nosys ach-nogame gach-box gach-list gach-p2; do
        for p in top bot; do
            case $s-$p in gach-*-top) continue;; esac	# (the game panel's other screen is the game's)
            f=$OUT/$size-$s-$p.png
            [ -s "$f" ] || { echo "FAIL $size: no $f"; fail=1; continue; }
            [ "$(convert "$f" -format '%[fx:standard_deviation>0.01]' info: 2>/dev/null)" = 1 ] || { echo "FAIL $size: $f is blank"; fail=1; }
        done
    done
    # a page in the list moves the rows: the list scrolled
    [ "$(compare -metric AE "$OUT/$size-ach-list-bot.png" "$OUT/$size-ach-end-bot.png" null: 2>&1 | cut -d' ' -f1)" != 0 ] || { echo "FAIL $size: the list didn't scroll"; fail=1; }
    [ "$(compare -metric AE "$OUT/$size-gach-list-bot.png" "$OUT/$size-gach-p2-bot.png" null: 2>&1 | cut -d' ' -f1)" != 0 ] || { echo "FAIL $size: the panel's list didn't page"; fail=1; }
done
[ $fail = 0 ] && echo "achievements: all checks passed ($OUT)"
exit $fail

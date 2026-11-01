#!/bin/sh
# banana-sync (Settings > Network > Home server) against both kinds of server, on throwaway cards in $OUT/sync:
#   RomM     tests/fake-romm.py, a stand-in made from RomM 5.4.1's own endpoint code (always run)
#   Romhost  the real server (Homeport's ROM library), when its source is at $ROMHOST_SRC (default ~/romhost) and
#            $ROMHOST_PY imports aiohttp; skipped otherwise
# What must hold: pairing (RomM's device code, a client token's pairing code; Romhost's code from Homeport, UDP
# discovery), the games with their covers and gamelist entries in the card's layout, a gamelist's own entries and a
# file of the user's at a server path left alone, a system skipped on the handheld left as it is, a game gone from
# the server removed (unless changed on the card), and the saves both ways with nothing lost in a conflict.
HERE=$(cd "$(dirname "$0")/.." && pwd)
OUT=${OUT:-$HERE/tests/out}/sync
rm -rf "$OUT"; mkdir -p "$OUT"
fail=0
bad() { echo "FAIL sync: $*"; fail=1; }
BIN=$OUT/banana-sync
${CXX:-g++} -std=c++17 -O2 -Wall -Wno-unused-function -o "$BIN" "$HERE/src/sync.cpp" "$HERE/src/json.cpp" -ldl || { echo "FAIL sync: banana-sync doesn't build"; exit 1; }
PIDS=""
cleanup() { for p in $PIDS; do kill "$p" 2>/dev/null; done; }
trap cleanup EXIT INT TERM
port() { python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1])'; }
waitport() { for i in $(seq 50); do curl -s "http://127.0.0.1:$1$2" >/dev/null && return 0; sleep 0.1; done; return 1; }
# card <name>: a throwaway card (its /data); bs <card> <args>: banana-sync on it
card() { mkdir -p "$OUT/$1/banana" "$OUT/$1/run" "$OUT/$1/roms"; }
bs() { c=$OUT/$1; shift; BANANA_SYNC_NAME="test $c" "$BIN" --root "$c" --run "$c/run" --status "$c/run/sync.status" "$@"; }
st() { cat "$OUT/$1/run/sync.status" 2>/dev/null; }
has() { [ -f "$OUT/$1" ] || bad "no $1"; }

# ---------------------------------------------------------------- RomM
L=$OUT/romm-lib
mkdir -p "$L/nds" "$L/gba" "$L/psx/Disc Game (USA)" "$L/3ds" "$L/covers"
head -c 300000 /dev/urandom > "$L/nds/Peel Quest (USA).nds"
head -c 70000 /dev/urandom > "$L/gba/Banana Run (USA).gba"
head -c 50000 /dev/urandom > "$L/gba/Old Game (USA).gba"
head -c 5000 /dev/urandom > "$L/3ds/Not Ours (USA).3ds"
head -c 5000 /dev/urandom > "$L/psx/Disc Game (USA)/Disc Game (USA) (Disc 1).bin"
printf 'FILE "Disc Game (USA) (Disc 1).bin" BINARY\n' > "$L/psx/Disc Game (USA)/Disc Game (USA) (Disc 1).cue"
printf '\211PNG\r\n\032\nfake' > "$L/covers/102.png"          # Banana Run's cover (ids: 3ds 101, gba 102 103, nds 104, psx 105)
P=$(port)
python3 "$HERE/tests/fake-romm.py" "$P" "$L" & PIDS="$PIDS $!"
waitport "$P" /api/heartbeat || bad "fake RomM didn't start"
U=http://127.0.0.1:$P
card ra
mkdir -p "$OUT/ra/roms/gba"
printf '<?xml version="1.0"?>\n<gameList>\n\t<game>\n\t\t<path>./Mine.gba</path>\n\t\t<name>My own</name>\n\t</game>\n</gameList>\n' > "$OUT/ra/roms/gba/gamelist.xml"
echo "the user's own" > "$OUT/ra/roms/gba/Old Game (USA).gba"
[ "$(bs ra probe "127.0.0.1:$P" | cut -d' ' -f1)" = romm ] || bad "probe didn't see RomM"
bs ra pair "$U" > "$OUT/romm-pair.log" 2>&1 & PP=$!
for i in $(seq 30); do grep -q '^pair ' "$OUT/ra/run/sync.status" 2>/dev/null && break; sleep 0.2; done
UC=$(st ra | cut -d' ' -f2)
case "$(st ra)" in "pair $UC $U/pair/device?user_code=$UC") ;; *) bad "RomM pairing showed no code, or an address without it (#141): $(st ra)";; esac
curl -s -XPOST "$U/test/approve" -d "{\"user_code\": \"$UC\"}" >/dev/null
wait $PP || bad "RomM pairing failed: $(cat "$OUT/romm-pair.log")"
grep -q '"token": "rmm_' "$OUT/ra/banana/romsync.json" || bad "no RomM token on the card"
[ "$(stat -c %a "$OUT/ra/banana/romsync.json")" = 600 ] || bad "romsync.json (the token) is readable by others"
bs ra sync > "$OUT/romm-sync1.log" 2>&1 || bad "RomM sync 1: $(tail -3 "$OUT/romm-sync1.log")"
has "ra/roms/nds/Peel Quest (USA).nds"; has "ra/roms/gba/Banana Run (USA).gba"; has "ra/roms/gba/media/box/Banana Run (USA).png"
has "ra/roms/psx/Disc Game (USA).m3u"; has "ra/roms/psx/.discs/Disc Game (USA)/Disc Game (USA) (Disc 1).bin"
cmp -s "$L/nds/Peel Quest (USA).nds" "$OUT/ra/roms/nds/Peel Quest (USA).nds" || bad "the NDS ROM isn't the server's"
[ "$(cat "$OUT/ra/roms/psx/Disc Game (USA).m3u")" = ".discs/Disc Game (USA)/Disc Game (USA) (Disc 1).cue" ] || bad "PS1 m3u: $(cat "$OUT/ra/roms/psx/Disc Game (USA).m3u")"
[ -d "$OUT/ra/roms/3ds" ] && bad "a platform BananaOS has no system for was synced"
[ "$(cat "$OUT/ra/roms/gba/Old Game (USA).gba")" = "the user's own" ] || bad "the user's own file was overwritten"
grep -q "conflicts" "$OUT/romm-sync1.log" && grep -q "1 conflicts" "$OUT/romm-sync1.log" || bad "the user's file wasn't reported as a conflict"
G=$OUT/ra/roms/gba/gamelist.xml
grep -q '<name>My own</name>' "$G" || bad "the gamelist's own entry was lost"
grep -q '<path>./Banana Run (USA).gba</path>' "$G" && grep -q '<image>./media/box/Banana Run (USA).png</image>' "$G" && grep -q '<releasedate>20100101T000000</releasedate>' "$G" ||
    bad "gamelist entry: $(cat "$G")"
grep -q 'Old Game' "$G" && bad "a gamelist entry for a game that isn't ours"
bs ra sync > "$OUT/romm-sync2.log" 2>&1
grep -q "0 to download" "$OUT/romm-sync2.log" || bad "a second sync downloaded again: $(head -2 "$OUT/romm-sync2.log")"
[ "$(grep -c '<path>./Banana Run' "$G")" = 1 ] || bad "the entry was added twice"
# a client token's pairing code (RomM: Profile > Client tokens > Pair) on a second card
card rb
CODE=$(curl -s -XPOST "$U/test/paircode" | python3 -c 'import json,sys; print(json.load(sys.stdin)["code"])')
bs rb pair "$U" "$CODE" >/dev/null 2>&1 || bad "pairing with a client token's code failed: $(st rb)"
bs rb pair "$U" "WRONGCOD" >/dev/null 2>&1 && bad "a wrong pairing code was taken"
# choosing games (homesync_games = chosen): only the chosen game comes, the list says which are chosen and on the card,
# and un-choosing a game already on the card leaves it there
card rc
CODE=$(curl -s -XPOST "$U/test/paircode" | python3 -c 'import json,sys; print(json.load(sys.stdin)["code"])')
bs rc pair "$U" "$CODE" >/dev/null 2>&1 || bad "rc: pairing failed: $(st rc)"
printf 'homesync_games = chosen\n' > "$OUT/rc/banana/settings.conf"
printf 'gba/Banana Run (USA).gba\n' > "$OUT/rc/banana/homesync-games.txt"
bs rc sync > "$OUT/pick-sync1.log" 2>&1 || bad "pick sync: $(tail -3 "$OUT/pick-sync1.log")"
has "rc/roms/gba/Banana Run (USA).gba"
[ -e "$OUT/rc/roms/nds/Peel Quest (USA).nds" ] && bad "pick: a game that wasn't chosen came to the card"
bs rc games > "$OUT/pick-games.tsv" 2>&1 || bad "games: $(head -2 "$OUT/pick-games.tsv")"
grep -q "^gba	gba/Banana Run (USA).gba	.*	1	1$" "$OUT/pick-games.tsv" || bad "games: the chosen game not listed as chosen + on the card: $(cat "$OUT/pick-games.tsv")"
grep -q "^nds	nds/Peel Quest (USA).nds	.*	0	0$" "$OUT/pick-games.tsv" || bad "games: the other game not listed as not chosen + not on the card: $(cat "$OUT/pick-games.tsv")"
: > "$OUT/rc/banana/homesync-games.txt"
bs rc sync > "$OUT/pick-sync2.log" 2>&1 || bad "pick sync 2: $(tail -3 "$OUT/pick-sync2.log")"
has "rc/roms/gba/Banana Run (USA).gba"
# a sync killed while it runs (Stop, a shutdown): the status says "error stopped" at once, not "running" for 120 s
# (killed by its sync.pid, as the menu's Stop does: $! is a subshell under dash, Ubuntu's sh, and killing that would
# leave banana-sync running with the lock held, so every later sync of this card would say "already running")
bs rb sync > "$OUT/romm-kill.log" 2>&1 & KP=$!
for i in $(seq 300); do case "$(st rb)" in running*) [ -s "$OUT/rb/run/sync.pid" ] && break;; esac; sleep 0.01; done
if case "$(st rb)" in running*) [ -s "$OUT/rb/run/sync.pid" ];; *) false;; esac; then
    kill -TERM "$(cat "$OUT/rb/run/sync.pid")"; wait $KP 2>/dev/null
    [ "$(st rb)" = "error stopped" ] || bad "a killed sync left the status: $(st rb)"
else
    wait $KP; echo "note: the sync ended before it could be killed ($(st rb)): the kill case not checked this run"
fi
# a download cut short goes on from its .part (a Range request), and is checked whole
mkdir -p "$OUT/rb/roms/nds"; head -c 1000 "$L/nds/Peel Quest (USA).nds" > "$OUT/rb/roms/nds/Peel Quest (USA).nds.part"
printf 'homesync.gba = off\nhomesync.psx = off\n' > "$OUT/rb/banana/settings.conf"
bs rb sync >/dev/null 2>&1
curl -s "$U/test/log" | grep -q 'Peel%20Quest%20%28USA%29.nds? range=bytes=1000-' || bad "no Range request for the .part"
cmp -s "$L/nds/Peel Quest (USA).nds" "$OUT/rb/roms/nds/Peel Quest (USA).nds" || bad "the resumed download isn't the server's file"
[ -d "$OUT/rb/roms/gba" ] && bad "a skipped system was downloaded"
# BananaOS's nds-saves runs for each DraStic .dsv before the saves are planned (the .sav is the one that syncs)
mkdir -p "$OUT/rb/drastic/backup"; echo dsv > "$OUT/rb/drastic/backup/Peel Quest (USA).dsv"
printf '#!/bin/sh\necho "$1" >> "%s"\n' "$OUT/nds-saves.log" > "$OUT/nds-saves"; chmod +x "$OUT/nds-saves"
BANANA_NDS_SAVES=$OUT/nds-saves bs rb sync --saves-only >/dev/null 2>&1
grep -q "rb/roms/nds/Peel Quest (USA).nds$" "$OUT/nds-saves.log" 2>/dev/null || bad "nds-saves wasn't run for the .dsv"
rm -rf "$OUT/rb/roms/nds" "$OUT/rb/drastic"; : > "$OUT/rb/banana/settings.conf"
# saves: up from card A (RetroArch's flat saves/ and BananaDS's saves/nds), down to card B
mkdir -p "$OUT/ra/saves/nds"; echo "ds v1" > "$OUT/ra/saves/nds/Peel Quest (USA).sav"; echo "gba v1" > "$OUT/ra/saves/Banana Run (USA).srm"
echo bak > "$OUT/ra/saves/Banana Run (USA).srm.bak"; echo resume > "$OUT/ra/saves/nds/Peel Quest (USA).resume.sav"
bs ra sync --saves-only > "$OUT/romm-saves1.log" 2>&1
grep -q "2 saves up" "$OUT/romm-saves1.log" || bad "RomM saves up: $(tail -2 "$OUT/romm-saves1.log")"
curl -s "$U/test/state" | grep -q 'resume\|\.bak' && bad "a .bak or quick-resume file was uploaded"
mkdir -p "$OUT/rb/roms/nds"; cp "$OUT/ra/roms/nds/Peel Quest (USA).nds" "$OUT/rb/roms/nds/"
bs rb sync --saves-only > "$OUT/romm-saves2.log" 2>&1
[ "$(cat "$OUT/rb/saves/nds/Peel Quest (USA).sav" 2>/dev/null)" = "ds v1" ] || bad "card B didn't get the DS save"
[ "$(cat "$OUT/rb/saves/Banana Run (USA).srm" 2>/dev/null)" = "gba v1" ] || bad "card B didn't get the GBA save"
# changed on B, synced; A gets it
echo "ds v2 from B" > "$OUT/rb/saves/nds/Peel Quest (USA).sav"; bs rb sync --saves-only >/dev/null 2>&1
bs ra sync --saves-only >/dev/null 2>&1
[ "$(cat "$OUT/ra/saves/nds/Peel Quest (USA).sav")" = "ds v2 from B" ] || bad "card A didn't get B's change"
# both changed: the newer wins, the other kept beside it as .bak (never lost), and it's counted
echo "gba A" > "$OUT/ra/saves/Banana Run (USA).srm"; sleep 1.1; echo "gba B newer" > "$OUT/rb/saves/Banana Run (USA).srm"
bs rb sync --saves-only >/dev/null 2>&1
bs ra sync --saves-only > "$OUT/romm-conflict.log" 2>&1
grep -q "1 conflicts" "$OUT/romm-conflict.log" || bad "the conflict wasn't counted: $(tail -1 "$OUT/romm-conflict.log")"
[ "$(cat "$OUT/ra/saves/Banana Run (USA).srm")" = "gba B newer" ] || bad "the newer save didn't win"
ls "$OUT/ra/saves/" | grep -q 'Banana Run (USA).srm.card-.*\.bak' || bad "the card's save wasn't kept in a conflict"
# a save deleted on the card is forgotten, not deleted on the server
rm "$OUT/rb/saves/nds/Peel Quest (USA).sav"; bs rb sync --saves-only >/dev/null 2>&1
curl -s "$U/test/state" | grep -q 'Peel Quest (USA).sav' || bad "deleting a save on the card deleted it on the server"
# a system skipped on this handheld: left as it is; a game gone from the server: removed; changed on the card: kept
printf 'homesync.nds = off\n' > "$OUT/ra/banana/settings.conf"
curl -s -XPOST "$U/test/delete-rom" -d '{"id": 104}' >/dev/null      # Peel Quest (nds, skipped)
curl -s -XPOST "$U/test/delete-rom" -d '{"id": 102}' >/dev/null      # Banana Run (gba)
bs ra sync > "$OUT/romm-sync3.log" 2>&1
has "ra/roms/nds/Peel Quest (USA).nds"
[ -f "$OUT/ra/roms/gba/Banana Run (USA).gba" ] && bad "a game removed on the server stayed"
grep -q 'Banana Run (USA).gba' "$G" && bad "its gamelist entry stayed"
grep -q '<name>My own</name>' "$G" || bad "the gamelist's own entry was lost on removal"
printf 'homesync.nds = on\n' > "$OUT/ra/banana/settings.conf"
echo patched >> "$OUT/ra/roms/nds/Peel Quest (USA).nds"
bs ra sync > "$OUT/romm-sync4.log" 2>&1
has "ra/roms/nds/Peel Quest (USA).nds"
grep -q "changed on the card, kept" "$OUT/romm-sync4.log" || bad "a ROM changed on the card wasn't kept: $(tail -3 "$OUT/romm-sync4.log")"
# an empty server listing never empties the card
rm -rf "$L/nds" "$L/gba" "$L/psx"; bs ra sync > "$OUT/romm-sync5.log" 2>&1
has "ra/roms/psx/Disc Game (USA).m3u"
bs ra status | grep -q "paired with RomM 5.4.1" || bad "status: $(bs ra status)"
bs ra forget; [ -f "$OUT/ra/banana/romsync.json" ] && bad "forget left the pairing"

# ---------------------------------------------------------------- Romhost
RS=${ROMHOST_SRC:-$HOME/romhost}; RPY=${ROMHOST_PY:-python3}
if [ -f "$RS/server.py" ] && "$RPY" -c 'import aiohttp, PIL' 2>/dev/null; then
    RP=$(port); RD=$OUT/romhost
    ( cd "$RS" && ROMHOST_DATA=$RD/data ROMHOST_CONFIG_DIR=$RD/conf ROMHOST_PORT=$RP exec "$RPY" server.py > "$OUT/romhost.log" 2>&1 ) & PIDS="$PIDS $!"
    waitport "$RP" /api/v1/hello || bad "Romhost didn't start: $(tail -3 "$OUT/romhost.log")"
    TOK=$(cat "$RD/conf/admin-token")
    adm() { m=$1; p=$2; shift 2; curl -s -X "$m" -H "X-Romhost-Admin: $TOK" "http://127.0.0.1:$RP$p" "$@"; }
    head -c 70000 /dev/urandom > "$OUT/rh.gba"; adm PUT "/admin/upload?name=Banana%20Run%20(USA).gba" --data-binary @"$OUT/rh.gba" >/dev/null
    for i in $(seq 100); do adm GET /admin/games | grep -q 'Banana Run' && break; sleep 0.3; done
    card ha; card hb
    BANANA_SYNC_DISCOVER_ADDR=127.0.0.1 BANANA_SYNC_DISCOVER_PORT=$RP bs ha discover | grep -q "^ROMHOST	http://127.0.0.1:$RP	" || bad "Romhost discovery found nothing"
    [ "$(bs ha probe "127.0.0.1:$RP" | cut -d' ' -f1)" = romhost ] || bad "probe didn't see Romhost"
    code() { adm POST /admin/devices/pair | python3 -c 'import json,sys; print(json.load(sys.stdin)["code"])'; }
    bs ha pair "127.0.0.1:$RP" 000000 >/dev/null 2>&1 && bad "Romhost took a wrong code"
    bs ha pair "127.0.0.1:$RP" "$(code)" >/dev/null 2>&1 || bad "Romhost pairing: $(st ha)"
    bs hb pair "127.0.0.1:$RP" "$(code)" >/dev/null 2>&1 || bad "Romhost pairing (card B): $(st hb)"
    bs ha sync > "$OUT/rh-sync1.log" 2>&1 || bad "Romhost sync: $(tail -3 "$OUT/rh-sync1.log")"
    cmp -s "$OUT/rh.gba" "$OUT/ha/roms/gba/Banana Run (USA).gba" || bad "Romhost: the GBA ROM isn't the server's"
    grep -q '<path>./Banana Run (USA).gba</path>' "$OUT/ha/roms/gba/gamelist.xml" || bad "Romhost: no gamelist entry"
    bs ha sync 2>&1 | grep -q "roms: up to date" || bad "Romhost: the manifest's ETag wasn't kept"
    adm GET /admin/devices | grep -q '"client": "banana-sync 1.0"' || bad "Romhost: no report from the device"
    # saves: the server's plan (romsync.py's protocol), versions on the server, conflicts kept for Homeport
    mkdir -p "$OUT/ha/saves/gba"; echo "rh v1" > "$OUT/ha/saves/gba/Banana Run (USA).srm"
    bs ha sync --saves-only 2>&1 | grep -q "1 saves up" || bad "Romhost: save not uploaded"
    bs hb sync --saves-only >/dev/null 2>&1
    [ "$(cat "$OUT/hb/saves/gba/Banana Run (USA).srm" 2>/dev/null)" = "rh v1" ] || bad "Romhost: card B didn't get the save"
    echo "rh A2" > "$OUT/ha/saves/gba/Banana Run (USA).srm"; echo "rh B2" > "$OUT/hb/saves/gba/Banana Run (USA).srm"
    bs ha sync --saves-only >/dev/null 2>&1
    bs hb sync --saves-only > "$OUT/rh-conflict.log" 2>&1
    grep -q "1 conflicts" "$OUT/rh-conflict.log" || bad "Romhost: no conflict: $(tail -2 "$OUT/rh-conflict.log")"
    [ "$(cat "$OUT/hb/saves/gba/Banana Run (USA).srm")" = "rh B2" ] || bad "Romhost: the card's save was replaced in a conflict"
    adm GET /admin/saves | grep -q '"conflicts": \[{' || bad "Romhost: the conflict isn't waiting in Homeport"
    # romsync.py (the reference client) carries on from banana-sync's state file
    if [ -f "$RS/client/romsync.py" ]; then
        python3 "$RS/client/romsync.py" --config "$OUT/ha/banana/romsync.json" sync --roms "$OUT/ha/roms" 2>&1 | grep -q "up to date" ||
            bad "romsync.py doesn't read banana-sync's state"
    fi
else
    echo "SKIP sync: Romhost (no $RS/server.py with aiohttp; ROMHOST_SRC / ROMHOST_PY)"
fi
# ---------------------------------------------------------------- the menu's page (Network > Home server)
if [ -x "$HERE/banana-ui" ]; then
    FX=$OUT/fixture; "$HERE/tests/fixture.sh" "$FX" >/dev/null
    printf 'wifi = on\n' >> "$FX/data/settings.conf"
    SL=$OUT/fake-sync.log; : > "$SL"
    # Settings > Network (the fifth tile) > its last row (Home server) > Server: A looks on the LAN, finds the stand-in's
    # server and asks for its code (typed: 1, START); then Sync now
    BANANA_SYNC="$HERE/tests/fake-sync.sh" FAKE_SYNC_LOG="$SL" "$HERE/banana-ui" --headless "$OUT/ui-" --size 640x480 --assets "$HERE/assets/rnds" \
        --splash "$HERE/assets/splash" --banana "$HERE/assets/banana" --systems "$HERE/../system/rootfs/etc/banana/systems.conf" \
        --roms "$FX/roms" --data "$FX/data" \
        --script "home 0; wait 2400; press start; press r; press r; press r; press r; press a; press up; shot hs-row; press a; shot hs-page; press a; wait 300; shot hs-code; press a; press start; wait 300; shot hs-paired; press down; shot hs-sync; press b; press b" \
        2> "$OUT/ui.log" || bad "banana-ui (Home server) exited $?"
    grep -q '^discover' "$SL" || bad "the Server row didn't look on the LAN: $(cat "$SL")"
    grep -q 'pair http://192.168.50.96:8092 .' "$SL" || bad "the code wasn't sent to pair: $(cat "$SL")"
    grep -q '"token": "rh1_test"' "$FX/data/romsync.json" 2>/dev/null || bad "no pairing written"
    for s in hs-row hs-page hs-code hs-paired hs-sync; do
        [ -s "$OUT/ui-$s-top.png" ] || bad "no screenshot $s"
    done
    cmp -s "$OUT/ui-hs-row-top.png" "$OUT/ui-hs-page-top.png" && bad "A on Home server didn't open its page"
fi
[ $fail = 0 ] && echo "banana-sync: all checks passed"
exit $fail

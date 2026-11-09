#!/bin/sh
# banana-sync (Settings > Network > Home server) against both kinds of server, on throwaway cards in $OUT/sync:
#   RomM     tests/fake-romm.py, a stand-in made from RomM 5.4.1's own endpoint code (always run)
#   Romhost  the real server (Homeport's ROM library), when its source is at $ROMHOST_SRC (default ~/romhost) and
#            $ROMHOST_PY imports aiohttp; skipped otherwise
# What must hold: pairing (RomM's device code, a client token's pairing code; Romhost's code from Homeport, UDP
# discovery), the games with their covers and gamelist entries in the card's layout, a gamelist's own entries and a
# file of the user's at a server path left alone, RomM's Get games (only what was asked for comes, once, and stays the
# owner's: a game gone from the server or deleted from the card is left so), a RomM before 5.4 only giving its saves (one changed on both sides
# held, both kept), and Romhost's saves both ways with nothing lost in a conflict. RomM's saves by slot are
# tests/sync-slots.sh's.
HERE=$(cd "$(dirname "$0")/.." && pwd)
OUT=${OUT:-$HERE/tests/out}/sync
rm -rf "$OUT"; mkdir -p "$OUT"
fail=0
bad() { echo "FAIL sync: $*"; fail=1; }
BIN=$OUT/banana-sync
${CXX:-g++} -std=c++17 -O2 -Wall -Wno-unused-function -o "$BIN" "$HERE/src/sync.cpp" "$HERE/src/sync_core.cpp" "$HERE/src/sync_romm.cpp" "$HERE/src/get_list.cpp" "$HERE/src/save_adapt.cpp" "$HERE/src/save_paths.cpp" "$HERE/src/save_vault.cpp" "$HERE/src/json.cpp" -ldl || { echo "FAIL sync: banana-sync doesn't build"; exit 1; }
PIDS=""
cleanup() { for p in $PIDS; do kill "$p" 2>/dev/null; done; }
trap cleanup EXIT INT TERM
port() { python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1])'; }
waitport() { for i in $(seq 50); do curl -s "http://127.0.0.1:$1$2" >/dev/null && return 0; sleep 0.1; done; return 1; }
# card <name>: a throwaway card (its /data); bs <card> <args>: banana-sync on it
card() { mkdir -p "$OUT/$1/banana" "$OUT/$1/run" "$OUT/$1/roms"; }
# (the image's systems.conf, and no RetroArch cores: the systems whose save folder is a core's are said, not synced)
export BANANA_SYSTEMS="$HERE/../system/rootfs/etc/banana/systems.conf" BANANA_LIBRETRO="$OUT/no-cores"
# (RomM's games come only by Get games: before a sync run by hand, every game the server lists for a system the card
# syncs is asked for, as the cases here were written for a card holding them all, unless GETALL=0, a case asking for
# its own)
getall() { skip=" $(sed -n 's/^homesync\.\([a-z0-9]*\) *= *off.*/\1/p' "$1/banana/settings.conf" 2>/dev/null | tr '\n' ' ') "
    "$BIN" --root "$1" --run "$1/run" games 2>/dev/null | awk -F'\t' -v s="$skip" 'index(s, " " $1 " ") == 0 { print $2 }' |
        tr '\n' '\0' | xargs -0 -r "$BIN" --root "$1" --run "$1/run" get >/dev/null; }
bs() { c=$OUT/$1; shift
    case " $* " in *" --rom "*|*" --saves-only "*|*" --auto "*) ;; " sync "*)
        [ "${GETALL:-1}" = 1 ] && grep -qs '"backend": *"romm"' "$c/banana/romsync.json" && getall "$c" ;; esac
    BANANA_SYNC_NAME="test $c" "$BIN" --root "$c" --run "$c/run" --status "$c/run/sync.status" "$@"; }
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
case "$(st ra)" in "pair $UC $U/pair/device?user_code=$UC 600") ;; *) bad "RomM pairing showed no code, the address without it (#141), or not the time it lasts: $(st ra)";; esac
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
# a system that makes its own covers (systems.conf: covers = own, as Game & Watch): its game comes, the server's cover
# and the gamelist's picture for it don't
card ro
CODE=$(curl -s -XPOST "$U/test/paircode" | python3 -c 'import json,sys; print(json.load(sys.stdin)["code"])')
bs ro pair "$U" "$CODE" >/dev/null 2>&1 || bad "ro: pairing failed: $(st ro)"
printf '[gba]\nname = Game Boy Advance\ncovers = own\n' > "$OUT/ro/systems.conf"
bs ro --systems "$OUT/ro/systems.conf" sync > "$OUT/own-covers.log" 2>&1 || bad "own covers sync: $(tail -3 "$OUT/own-covers.log")"
has "ro/roms/gba/Banana Run (USA).gba"
[ -e "$OUT/ro/roms/gba/media/box/Banana Run (USA).png" ] && bad "covers = own: the server's cover was downloaded"
grep -q '<image>' "$OUT/ro/roms/gba/gamelist.xml" && bad "covers = own: the gamelist names a cover that isn't there"
# a client token's pairing code (RomM: Profile > Client tokens > Pair) on a second card
card rb
CODE=$(curl -s -XPOST "$U/test/paircode" | python3 -c 'import json,sys; print(json.load(sys.stdin)["code"])')
bs rb pair "$U" "$CODE" >/dev/null 2>&1 || bad "pairing with a client token's code failed: $(st rb)"
bs rb pair "$U" "WRONGCOD" >/dev/null 2>&1 && bad "a wrong pairing code was taken"
# each card a RomM device of its own, whichever way it paired (the device flow, a pairing code, a token pasted), as
# bananaos-<the card's banana/sync-id>; the same device when it pairs again; registered again when deleted in RomM; no
# saves while RomM has its sync switched off
mint() { curl -s -XPOST "$U/test/token" -d "${1:-{\}}" | python3 -c 'import json,sys; print(json.load(sys.stdin)["token"])'; }
AT=$(mint)
devs() { curl -s -H "Authorization: Bearer $AT" "$U/api/devices"; }
devof() { python3 -c 'import json,sys; print(json.load(open(sys.argv[1])).get("device") or "")' "$OUT/$1/banana/romsync.json"; }
dev() { devs | python3 -c 'import json,sys; d = [x for x in json.load(sys.stdin) if x["id"] == sys.argv[1]]; print(" ".join(str(d[0][k]) for k in ("hostname", "client", "platform", "sync_mode")) if d else "none")' "$1"; }
ndevs() { devs | python3 -c 'import json,sys; print(len(json.load(sys.stdin)))'; }
DA=$(devof ra); DB=$(devof rb)
[ "$(dev "$DA")" = "bananaos-$(cat "$OUT/ra/banana/sync-id") bananaos BananaOS api" ] || bad "card A's device (the device flow): $(dev "$DA")"
[ -n "$DB" ] && [ "$DB" != "$DA" ] && [ "$(dev "$DB")" = "bananaos-$(cat "$OUT/rb/banana/sync-id") bananaos BananaOS api" ] || bad "card B (a pairing code) has no device of its own: $DB $(dev "$DB")"
card rp; bs rp pair "$U" "$(mint)" >/dev/null 2>&1 || bad "pairing with a pasted token failed: $(st rp)"
DD=$(devof rp)
[ -n "$DD" ] && [ "$(dev "$DD")" = "bananaos-$(cat "$OUT/rp/banana/sync-id") bananaos BananaOS api" ] || bad "a pasted token's card has no device: $DD"
n=$(ndevs); CODE=$(curl -s -XPOST "$U/test/paircode" | python3 -c 'import json,sys; print(json.load(sys.stdin)["code"])')
# (named otherwise this time: the device held is brought up to date, which a registration RomM matches by hostname isn't)
c=$OUT/ra; BANANA_SYNC_NAME="RG DS renamed" "$BIN" --root "$c" --run "$c/run" --status "$c/run/sync.status" pair "$U" "$CODE" >/dev/null 2>&1 || bad "card A pairing again failed: $(st ra)"
[ "$(devof ra)" = "$DA" ] && [ "$(ndevs)" = "$n" ] || bad "pairing again made another device: $(devof ra), $(ndevs) devices (were $n)"
devs | grep -q '"name": "RG DS renamed"' || bad "the device held wasn't brought up to date when the card paired again"
# (rename: the name in RomM's Devices at once and kept as the pairing's; a sync after it doesn't write the old one back;
# an empty name or one on a card not paired changes nothing)
[ "$(bs ra rename 'Living room DS')" = ok ] || bad "rename: $(bs ra rename 'Living room DS')"
devs | grep -q '"name": "Living room DS"' || bad "rename: RomM's device not renamed"
grep -q '"name": "Living room DS"' "$OUT/ra/banana/romsync.json" || bad "rename: the name not kept on the card"
bs ra sync --saves-only > "$OUT/romm-rename.log" 2>&1
grep -q '"name": "Living room DS"' "$OUT/ra/banana/romsync.json" || bad "rename: a sync wrote the old name back: $(grep name "$OUT/ra/banana/romsync.json")"
[ "$(bs ra rename '  ')" = "error a name of 1 to 63 characters" ] || bad "rename: an empty name taken"
card rn; [ "$(bs rn rename x)" = "error not paired" ] || bad "rename: a card not paired renamed"
curl -s -XDELETE -H "Authorization: Bearer $AT" "$U/api/devices/$DD" >/dev/null
bs rp sync --saves-only > "$OUT/romm-dev1.log" 2>&1
grep -q 'the device was gone from RomM' "$OUT/romm-dev1.log" && grep -q 'registered as RomM device' "$OUT/romm-dev1.log" && [ "$(dev "$(devof rp)")" != none ] || bad "a deleted device wasn't registered again: $(cat "$OUT/romm-dev1.log")"
curl -s -XPUT -H "Authorization: Bearer $AT" -H 'Content-Type: application/json' -d '{"sync_enabled": false}' "$U/api/devices/$(devof rp)" >/dev/null
printf 'homesync.nds = off\nhomesync.psx = off\n' > "$OUT/rp/banana/settings.conf"
bs rp sync > "$OUT/romm-dev2.log" 2>&1
grep -q 'save sync is off for this handheld in RomM (Devices)' "$OUT/romm-dev2.log" || bad "RomM's sync switched off not said: $(cat "$OUT/romm-dev2.log")"
has "rp/roms/gba/Banana Run (USA).gba"
# a token that can't touch devices: paired all the same, its syncs say so and aren't errors
card rq; bs rq pair "$U" "$(mint '{"scopes": ["me.read", "roms.read", "platforms.read", "assets.read", "assets.write"]}')" > "$OUT/romm-dev3.log" 2>&1 ||
    bad "a token without the devices scopes didn't pair: $(cat "$OUT/romm-dev3.log")"
grep -q 'not registered as a device yet' "$OUT/romm-dev3.log" || bad "a pairing without a device not said: $(cat "$OUT/romm-dev3.log")"
bs rq sync --saves-only > "$OUT/romm-dev4.log" 2>&1 || bad "a sync without a device failed: $(cat "$OUT/romm-dev4.log")"
grep -q 'not registered as a RomM device' "$OUT/romm-dev4.log" && ! grep -q 'error:' "$OUT/romm-dev4.log" || bad "a sync without a device: $(cat "$OUT/romm-dev4.log")"
# a dry run registers nothing
curl -s -XDELETE -H "Authorization: Bearer $AT" "$U/api/devices/$(devof rp)" >/dev/null; n=$(ndevs)
bs rp sync --saves-only --dry-run > "$OUT/romm-dev5.log" 2>&1
grep -q 'would register as a RomM device' "$OUT/romm-dev5.log" && ! grep -q 'registered' "$OUT/romm-dev5.log" && [ "$(ndevs)" = "$n" ] || bad "a dry run registered a device: $(ndevs) (were $n) $(cat "$OUT/romm-dev5.log")"
# a card that can't keep its id (banana/sync-id unwritable): no pairing at all, rather than one device for all of them
card rw; mkdir "$OUT/rw/banana/sync-id"
bs rw pair "$U" "$(mint)" >/dev/null 2>&1 && bad "paired without the card's id"
[ "$(st rw)" = "error can't write banana/sync-id" ] && [ "$(ndevs)" = "$n" ] || bad "no card id: $(st rw), $(ndevs) devices (were $n)"
# Get games (RomM): only the game asked for comes, with its gamelist entry, and leaves the list once it's in; the list
# says which wait and which are on the card; a game taken back before a sync doesn't come; one deleted from the card
# afterwards isn't brought back, and one gone from the server stays: once on the card a game is the owner's
card rc
CODE=$(curl -s -XPOST "$U/test/paircode" | python3 -c 'import json,sys; print(json.load(sys.stdin)["code"])')
bs rc pair "$U" "$CODE" >/dev/null 2>&1 || bad "rc: pairing failed: $(st rc)"
[ "$(bs rc get 'gba/Banana Run (USA).gba' 'nds/Peel Quest (USA).nds')" = ok ] || bad "get: games not added to the list"
[ "$(bs rc get --drop 'nds/Peel Quest (USA).nds')" = ok ] || bad "get --drop: a game not taken back"
[ "$(bs rc get '../x')" = "error not a game: ../x" ] || bad "get: a path out of the roms folder taken"
bs rc games > "$OUT/pick-games0.tsv" 2>&1
grep -q "^gba	gba/Banana Run (USA).gba	.*	1	0$" "$OUT/pick-games0.tsv" || bad "games: the game asked for not listed as waiting: $(cat "$OUT/pick-games0.tsv")"
GETALL=0 bs rc sync > "$OUT/pick-sync1.log" 2>&1 || bad "get sync: $(tail -3 "$OUT/pick-sync1.log")"
has "rc/roms/gba/Banana Run (USA).gba"
grep -q 'Banana Run' "$OUT/rc/roms/gba/gamelist.xml" 2>/dev/null || bad "get: no gamelist entry for the game asked for"
[ -e "$OUT/rc/roms/nds/Peel Quest (USA).nds" ] && bad "get: a game taken back (or never asked for) came to the card"
grep -q 'Banana Run' "$OUT/rc/banana/homesync-get.txt" && bad "get: the game stayed on the list once in: $(cat "$OUT/rc/banana/homesync-get.txt")"
bs rc games > "$OUT/pick-games.tsv" 2>&1 || bad "games: $(head -2 "$OUT/pick-games.tsv")"
grep -q "^gba	gba/Banana Run (USA).gba	.*	0	1$" "$OUT/pick-games.tsv" || bad "games: the game got not listed as on the card: $(cat "$OUT/pick-games.tsv")"
grep -q "^nds	nds/Peel Quest (USA).nds	.*	0	0$" "$OUT/pick-games.tsv" || bad "games: the other game not listed as not waiting + not on the card: $(cat "$OUT/pick-games.tsv")"
# (the server's games kept on the card for Get games' page: games writes them whole, under the server's address; a
# sync replaces the systems it listed and keeps a skipped one's; one game's sync leaves them; Forget takes them away)
GC=$OUT/rc/banana/homesync-server-games.tsv
head -1 "$GC" 2>/dev/null | grep -q "^# $U	[0-9][0-9]*$" || bad "games: the list not kept on the card under the server: $(head -1 "$GC" 2>&1)"
# (but for its last column, on the card, which the page looks at for itself: 0 in what is kept)
[ "$(tail -n +2 "$GC")" = "$(grep '	' "$OUT/pick-games.tsv" | sed 's/	[01]$/	0/')" ] || bad "games: the kept list isn't what was printed: $(cat "$GC")"
printf 'gba\tgba/Gone (USA).gba\tGone\t1\t0\t0\n' >> "$GC"
cp "$GC" "$OUT/gc-before.tsv"
bs rc sync --rom "gba/Banana Run (USA).gba" > "$OUT/pick-gc-rom.log" 2>&1
cmp -s "$GC" "$OUT/gc-before.tsv" || bad "a game's own sync rewrote the kept list: $(cat "$GC")"
printf 'homesync.nds = off\n' >> "$OUT/rc/banana/settings.conf"
bs rc sync --saves-only > "$OUT/pick-gc-sync.log" 2>&1 || bad "gc sync: $(tail -3 "$OUT/pick-gc-sync.log")"
grep -q 'Gone (USA)' "$GC" && bad "a sync didn't replace the kept list's games of a system it listed: $(cat "$GC")"
grep -q '^nds	nds/Peel Quest (USA).nds	' "$GC" || bad "a sync dropped a skipped system's games from the kept list: $(cat "$GC")"
grep -q '^gba	gba/Banana Run (USA).gba	' "$GC" || bad "a sync's kept list lost a listed system's game: $(cat "$GC")"
sed -i '/^homesync\.nds/d' "$OUT/rc/banana/settings.conf"
cp -r "$OUT/rc" "$OUT/rcf"
printf 'gba/Asked (USA).gba\n' >> "$OUT/rcf/banana/homesync-get.txt"; printf 'gba/Asked (USA).gba\tchecksum mismatch\n' >> "$OUT/rcf/banana/homesync-get-failed.txt"
bs rcf forget >/dev/null 2>&1
[ -e "$OUT/rcf/banana/homesync-server-games.tsv" ] && bad "Forget left the server's kept games"
[ -e "$OUT/rcf/banana/homesync-get.txt" ] || [ -e "$OUT/rcf/banana/homesync-get-failed.txt" ] && bad "Forget left the games asked of the server"
# (paired again with the same server: what was asked of it still waits; paired with another one, without a Forget
# first: the last one's games, list and failures gone as a Forget takes them, or its next sync would bring them)
cp -r "$OUT/rc" "$OUT/rcp"
printf 'gba/Asked (USA).gba\n' >> "$OUT/rcp/banana/homesync-get.txt"; printf 'gba/Asked (USA).gba\tchecksum mismatch\n' >> "$OUT/rcp/banana/homesync-get-failed.txt"
CODE=$(curl -s -XPOST "$U/test/paircode" | python3 -c 'import json,sys; print(json.load(sys.stdin)["code"])')
bs rcp pair "$U" "$CODE" >/dev/null 2>&1 || bad "rcp: pairing again failed: $(st rcp)"
grep -qx 'gba/Asked (USA).gba' "$OUT/rcp/banana/homesync-get.txt" 2>/dev/null || bad "paired again with the same server: the games asked of it dropped"
[ -e "$OUT/rcp/banana/homesync-server-games.tsv" ] || bad "paired again with the same server: its kept games dropped"
P2=$(port)
python3 "$HERE/tests/fake-romm.py" "$P2" "$L" 2> "$OUT/fake-romm-2.log" & PIDS="$PIDS $!"
waitport "$P2" /api/heartbeat || bad "the second fake RomM didn't start"
CODE=$(curl -s -XPOST "http://127.0.0.1:$P2/test/paircode" | python3 -c 'import json,sys; print(json.load(sys.stdin)["code"])')
bs rcp pair "http://127.0.0.1:$P2" "$CODE" >/dev/null 2>&1 || bad "rcp: pairing with another server failed: $(st rcp)"
[ -e "$OUT/rcp/banana/homesync-server-games.tsv" ] && bad "paired with another server: the last one's kept games left"
[ -e "$OUT/rcp/banana/homesync-get.txt" ] || [ -e "$OUT/rcp/banana/homesync-get-failed.txt" ] && bad "paired with another server: the games asked of the last one left"
# (a server that doesn't answer at all, as with Wi-Fi not back yet: the sync ends at once as that, said in words, not
# as each part failing in turn)
cp -r "$OUT/rc" "$OUT/rcn"
python3 -c 'import json,sys; p=sys.argv[1]; j=json.load(open(p)); j["server"]="http://127.0.0.1:1"; json.dump(j, open(p, "w"))' "$OUT/rcn/banana/romsync.json"
bs rcn sync --saves-only > "$OUT/noconnect.log" 2>&1 && bad "a sync with the server unreachable said it worked"
case $(st rcn) in "error noconnect "?*) ;; *) bad "the server unreachable: $(st rcn)" ;; esac
grep -q 'the server can.t be reached' "$OUT/noconnect.log" || bad "the server unreachable not said: $(cat "$OUT/noconnect.log")"
# (games asked for while a sync runs: brought by it, the saves' look at the library held up so the list changes mid-run)
curl -s -XPOST "$U/test/fault" -d '{"path": "GET /api/roms", "kind": "delay", "arg": 2}' >/dev/null
GETALL=0 bs rc sync > "$OUT/pick-sync3.log" 2>&1 & _k=$!
sleep 1; bs rc get 'nds/Peel Quest (USA).nds' >/dev/null; wait $_k
has "rc/roms/nds/Peel Quest (USA).nds"
rm "$OUT/rc/roms/gba/Banana Run (USA).gba"
GETALL=0 bs rc sync > "$OUT/pick-sync2.log" 2>&1 || bad "get sync 2: $(tail -3 "$OUT/pick-sync2.log")"
[ -e "$OUT/rc/roms/gba/Banana Run (USA).gba" ] && bad "get: a game deleted from the card was brought back"
# (a download that fails: the game stays on the list, why said in homesync-get-failed.txt for the page; the next sync
# brings it and the line goes)
bs rc get 'gba/Banana Run (USA).gba' >/dev/null
curl -s -XPOST "$U/test/fault" -d '{"path": "GET /api/roms/.*/content/Banana", "kind": "badhash", "count": 9}' >/dev/null
GETALL=0 bs rc sync > "$OUT/pick-fail.log" 2>&1
grep -q '^gba/Banana Run (USA).gba	.*mismatch' "$OUT/rc/banana/homesync-get-failed.txt" 2>/dev/null || bad "get: a failed download's reason not kept: $(cat "$OUT/rc/banana/homesync-get-failed.txt" 2>&1) $(tail -3 "$OUT/pick-fail.log")"
grep -qx 'gba/Banana Run (USA).gba' "$OUT/rc/banana/homesync-get.txt" || bad "get: a failed download left the list"
curl -s -XPOST "$U/test/fault" -d '{"clear": true}' >/dev/null
GETALL=0 bs rc sync > "$OUT/pick-fail2.log" 2>&1
has "rc/roms/gba/Banana Run (USA).gba"
grep -q 'Banana Run' "$OUT/rc/banana/homesync-get-failed.txt" && bad "get: a game that came kept its failure: $(cat "$OUT/rc/banana/homesync-get-failed.txt")"
# (two failing, the first a GBA game, the second the disc set by a file other than the one it is listed by: each
# game's line is its own, never the other's error)
DK=$(bs rc games | awk -F'\t' '$1 == "psx" { print $2 }')
case $DK in *.bin) DX=cue ;; *) DX=bin ;; esac
rm -rf "$OUT/rc/roms/gba/Banana Run (USA).gba" "$OUT/rc/roms/psx/Disc Game (USA)"*
bs rc get 'gba/Banana Run (USA).gba' "$DK" >/dev/null
curl -s -XPOST "$U/test/fault" -d '{"path": "GET /api/roms/.*/content/Banana", "kind": "badhash", "count": 9}' >/dev/null
curl -s -XPOST "$U/test/fault" -d "{\"path\": \"GET /api/roms/.*/files/content/.*\\\\.$DX\$\", \"kind\": \"badhash\", \"count\": 9}" >/dev/null
GETALL=0 bs rc sync > "$OUT/pick-fail3.log" 2>&1
curl -s -XPOST "$U/test/fault" -d '{"clear": true}' >/dev/null
DL=$(grep -F "$DK	" "$OUT/rc/banana/homesync-get-failed.txt" 2>/dev/null)
case $DL in *"Banana Run"*|"") bad "get: the disc set's failure not its own: '$DL' $(grep -i 'error\|mismatch' "$OUT/pick-fail3.log")" ;; esac
case $DL in *".$DX: "*) ;; *) bad "get: the disc set's failure doesn't name its file: '$DL'" ;; esac
grep -q '^gba/Banana Run (USA).gba	.*mismatch' "$OUT/rc/banana/homesync-get-failed.txt" || bad "get: the GBA game's failure: $(cat "$OUT/rc/banana/homesync-get-failed.txt")"
# (asked for again, by banana-sync get as by the page: that game's failure gone, the other's kept)
bs rc get 'gba/Banana Run (USA).gba' >/dev/null
grep -q 'Banana Run' "$OUT/rc/banana/homesync-get-failed.txt" && bad "get: a game asked for again kept its failure: $(cat "$OUT/rc/banana/homesync-get-failed.txt")"
grep -qF "$DK	" "$OUT/rc/banana/homesync-get-failed.txt" || bad "get: another game's failure went with it: $(cat "$OUT/rc/banana/homesync-get-failed.txt")"
GETALL=0 bs rc sync > "$OUT/pick-fail4.log" 2>&1
has "rc/roms/gba/Banana Run (USA).gba"
# (a game asked for during the saves comes after them, save.lock let go first: the sync after a game runs beside that
# download at once, not after a 10 s wait for the lock)
rm "$OUT/rc/roms/nds/Peel Quest (USA).nds"
curl -s -XPOST "$U/test/fault" -d '{"path": "GET /api/roms", "kind": "delay", "arg": 2}' >/dev/null
curl -s -XPOST "$U/test/fault" -d '{"path": "GET /api/roms/.*/content/Peel", "kind": "delay", "arg": 6}' >/dev/null
GETALL=0 bs rc sync > "$OUT/pick-late.log" 2>&1 & _k=$!
sleep 1; bs rc get 'nds/Peel Quest (USA).nds' >/dev/null; sleep 2.5
t=$(date +%s); bs rc sync --rom "gba/Banana Run (USA).gba" > "$OUT/pick-late-rom.log" 2>&1
[ $(( $(date +%s) - t )) -lt 4 ] && ! grep -q 'another sync is saving' "$OUT/pick-late-rom.log" ||
    bad "get: the sync after a game waited on a download after the saves: $(cat "$OUT/pick-late-rom.log")"
wait $_k
has "rc/roms/nds/Peel Quest (USA).nds"
# (a game asked for that is on the card already, copied by hand: adopted, nothing downloaded, and the gamelist entry
# its owner gave it kept. Get games' own run, --games-only: the game and not the saves, a save new on the card left for
# the next sync of the saves)
cp "$L/gba/Old Game (USA).gba" "$OUT/rc/roms/gba/"
sed -i 's|</gameList>|  <game><path>./Old Game (USA).gba</path><name>Mine</name></game>\n</gameList>|' "$OUT/rc/roms/gba/gamelist.xml"
mkdir -p "$OUT/rc/saves/gba"; printf 'old game save' > "$OUT/rc/saves/gba/Old Game (USA).srm"
bs rc get 'gba/Old Game (USA).gba' >/dev/null
GETALL=0 bs rc sync --games-only > "$OUT/pick-adopt.log" 2>&1
grep -q 'Old Game (USA).srm' "$OUT/rc/banana/romsync.json" && bad "sync --games-only synced a save: $(cat "$OUT/pick-adopt.log")"
st rc | grep -q '^done .*|games only' || bad "sync --games-only's line doesn't say so: $(st rc)"
GETALL=0 bs rc sync > "$OUT/pick-adopt2.log" 2>&1
grep -q 'Old Game (USA).srm' "$OUT/rc/banana/romsync.json" || bad "the save --games-only left wasn't synced by the next sync: $(cat "$OUT/pick-adopt2.log")"
grep -q '0 to download' "$OUT/pick-adopt.log" || bad "get: a game on the card already was downloaded: $(head -3 "$OUT/pick-adopt.log")"
grep -q '<name>Mine</name>' "$OUT/rc/roms/gba/gamelist.xml" || bad "get: an adopted game's own gamelist entry was replaced: $(grep -A1 'Old Game' "$OUT/rc/roms/gba/gamelist.xml")"
grep -q 'Old Game' "$OUT/rc/banana/homesync-get.txt" && bad "get: an adopted game stayed on the list"
# (a system set to Skip keeps its saves on the card in a sync that also brings games: Get games' part lists every
# system, and the saves after it go by Systems all the same)
grep -q 'Banana Run (USA).srm' "$OUT/rc/banana/romsync.json" && bad "skip: the GBA save was synced before the test"
head -c 8192 /dev/urandom > "$OUT/rc/saves/gba/Banana Run (USA).srm"
printf 'homesync.gba = off\n' >> "$OUT/rc/banana/settings.conf"
bs rc get 'nds/Peel Quest (USA).nds' >/dev/null
GETALL=0 bs rc sync > "$OUT/pick-skip.log" 2>&1
grep -q 'Banana Run (USA).srm' "$OUT/rc/banana/romsync.json" && bad "a sync bringing games synced a skipped system's save: $(grep -i 'banana run' "$OUT/pick-skip.log")"
# (and a game of that system asked for while the saves are going still comes, by the pass after them: the saves'
# leaving the system out isn't the list's)
rm "$OUT/rc/roms/gba/Old Game (USA).gba"
curl -s -XPOST "$U/test/fault" -d '{"path": "POST /api/sync/negotiate", "kind": "delay", "arg": 4}' >/dev/null
GETALL=0 bs rc sync > "$OUT/pick-skip2.log" 2>&1 & _k=$!
sleep 1.5; bs rc get 'gba/Old Game (USA).gba' >/dev/null
wait $_k
[ -f "$OUT/rc/roms/gba/Old Game (USA).gba" ] || bad "skip: a game of a skipped system asked for during the saves didn't come: $(grep -i 'old game\|roms:' "$OUT/pick-skip2.log")"
grep -q 'Old Game' "$OUT/rc/banana/homesync-get.txt" && bad "skip: a game asked for during the saves stayed on the list"
grep -q 'Banana Run (USA).srm' "$OUT/rc/banana/romsync.json" && bad "a sync bringing a skipped system's game synced its save: $(grep -i 'banana run' "$OUT/pick-skip2.log")"
sed -i '/^homesync\.gba/d' "$OUT/rc/banana/settings.conf"; rm "$OUT/rc/saves/gba/Banana Run (USA).srm"
# a stand-in of its own (its ids apart from the one above): a zipped hack, its base game and one title from two regions.
# A zip comes down whole, checked by its members' CRCs, as RomM's hashes of a zip are of what is inside; each game is
# listed (Choose games, the gamelist) by its title and its tags, the regions only where a title is shared
LZ=$OUT/romm-lib-tags
mkdir -p "$LZ/gba"
python3 -c 'import sys, zipfile; z = zipfile.ZipFile(sys.argv[1], "w", zipfile.ZIP_DEFLATED); z.writestr("Pocket Hack (USA) [Hack by Someone v1.0].gba", b"hacked rom " * 5000)' "$LZ/gba/Pocket Hack (USA) [Hack by Someone v1.0].zip"
head -c 40000 /dev/urandom > "$LZ/gba/Pocket Hack (USA).gba"
# (and a zip whose list of members isn't read here, its entry count not what its central directory holds, as zipMembers
# also finds zip64's: kept, not checked, rather than a mismatch thrown away and fetched again every sync)
python3 -c 'import sys, zipfile
z = zipfile.ZipFile(sys.argv[1], "w", zipfile.ZIP_DEFLATED); z.writestr("Odd Zip (USA).gba", b"odd rom " * 5000); z.close()
b = bytearray(open(sys.argv[1], "rb").read()); e = b.rfind(b"PK\x05\x06"); b[e + 8:e + 12] = (2).to_bytes(2, "little") * 2
open(sys.argv[1], "wb").write(b)' "$LZ/gba/Odd Zip (USA).zip"
head -c 30000 /dev/urandom > "$LZ/gba/Twin (USA).gba"
head -c 30000 /dev/urandom > "$LZ/gba/Twin (Japan).gba"
PZ=$(port)
python3 "$HERE/tests/fake-romm.py" "$PZ" "$LZ" & PIDS="$PIDS $!"
waitport "$PZ" /api/heartbeat || bad "the tags stand-in didn't start"
UZ=http://127.0.0.1:$PZ
card rz
bs rz pair "$UZ" "$(curl -s -XPOST "$UZ/test/token" -d '{}' | python3 -c 'import json,sys; print(json.load(sys.stdin)["token"])')" >/dev/null 2>&1 || bad "rz: pairing failed: $(st rz)"
bs rz sync > "$OUT/tags-sync.log" 2>&1 || bad "a zipped hack's sync: $(tail -3 "$OUT/tags-sync.log")"
has "rz/roms/gba/Pocket Hack (USA) [Hack by Someone v1.0].zip"
grep -q 'mismatch' "$OUT/tags-sync.log" && bad "a zip checked against RomM's hash of what's inside: $(cat "$OUT/tags-sync.log")"
has "rz/roms/gba/Odd Zip (USA).zip"
grep -q "Odd Zip (USA).zip: its members can't be read here, not checked" "$OUT/tags-sync.log" || bad "a zip whose members aren't read: $(grep -i 'odd zip' "$OUT/tags-sync.log")"
cmp -s "$LZ/gba/Pocket Hack (USA) [Hack by Someone v1.0].zip" "$OUT/rz/roms/gba/Pocket Hack (USA) [Hack by Someone v1.0].zip" || bad "the zip isn't the server's"
bs rz games > "$OUT/tags-games.tsv" 2>&1 || bad "tags games: $(head -2 "$OUT/tags-games.tsv")"
for t in "Pocket Hack [Hack by Someone v1.0]" "Pocket Hack" "Twin (USA)" "Twin (Japan)"; do
    cut -f3 "$OUT/tags-games.tsv" | grep -qxF "$t" || bad "games: no title '$t': $(cut -f3 "$OUT/tags-games.tsv" | tr '\n' '|')"
done
grep -qF '<name>Pocket Hack [Hack by Someone v1.0]</name>' "$OUT/rz/roms/gba/gamelist.xml" && grep -qF '<name>Twin (Japan)</name>' "$OUT/rz/roms/gba/gamelist.xml" ||
    bad "gamelist names: $(grep '<name>' "$OUT/rz/roms/gba/gamelist.xml" | tr -d '\t' | tr '\n' ' ')"
# games to the second SD card (homesync_roms_to = sd2; its roms folder is the one run/sd2/roms names, as the sd2
# script writes it): new games go there with their covers and gamelist, a game already on this card stays here, a
# card that's out holds its games back (never this card instead), and back in nothing downloads twice
card rd
CODE=$(curl -s -XPOST "$U/test/paircode" | python3 -c 'import json,sys; print(json.load(sys.stdin)["code"])')
bs rd pair "$U" "$CODE" >/dev/null 2>&1 || bad "rd: pairing failed: $(st rd)"
printf 'homesync_roms_to = sd2\n' > "$OUT/rd/banana/settings.conf"
S2=$OUT/rd-sd2/roms; mkdir -p "$S2" "$OUT/rd/run/sd2" "$OUT/rd/roms/nds"; echo "$S2" > "$OUT/rd/run/sd2/roms"; : > "$OUT/rd/run/sd2/slot"
cp "$L/nds/Peel Quest (USA).nds" "$OUT/rd/roms/nds/"
bs rd sync > "$OUT/sd2-sync1.log" 2>&1 || bad "sd2 sync: $(tail -3 "$OUT/sd2-sync1.log")"
[ -f "$S2/gba/Banana Run (USA).gba" ] && [ -f "$S2/gba/media/box/Banana Run (USA).png" ] || bad "sd2: the new game and its cover aren't on the second card"
grep -q '<path>./Banana Run (USA).gba</path>' "$S2/gba/gamelist.xml" 2>/dev/null || bad "sd2: no gamelist entry on the second card"
[ -e "$OUT/rd/roms/gba/Banana Run (USA).gba" ] && bad "sd2: a game for the second card came to this one"
[ -e "$S2/nds/Peel Quest (USA).nds" ] && bad "sd2: a game already on this card was downloaded to the second card"
bs rd games 2>/dev/null | grep -q "^gba	gba/Banana Run (USA).gba	.*	1$" || bad "sd2: games doesn't list the second card's game as on the handheld"
rm "$OUT/rd/run/sd2/roms"
bs rd sync > "$OUT/sd2-sync2.log" 2>&1
grep -q "the second SD card is out" "$OUT/sd2-sync2.log" || bad "sd2: the card being out wasn't reported: $(tail -2 "$OUT/sd2-sync2.log")"
[ -e "$OUT/rd/roms/gba/Banana Run (USA).gba" ] && bad "sd2: with the second card out, its game came to this card"
echo "$S2" > "$OUT/rd/run/sd2/roms"
bs rd sync > "$OUT/sd2-sync3.log" 2>&1 || bad "sd2 sync 3: $(tail -3 "$OUT/sd2-sync3.log")"
grep -q "0 to download" "$OUT/sd2-sync3.log" || bad "sd2: back in, a game downloaded again: $(head -2 "$OUT/sd2-sync3.log")"
# a game the sync put on this card, deleted from it since: it comes back as a new game, to the second card
rm "$OUT/rd/roms/nds/Peel Quest (USA).nds"
bs rd sync > "$OUT/sd2-sync4.log" 2>&1 || bad "sd2 sync 4: $(tail -3 "$OUT/sd2-sync4.log")"
[ -f "$S2/nds/Peel Quest (USA).nds" ] || bad "sd2: a game deleted from this card came back to it, not to the second card"
[ -e "$OUT/rd/roms/nds/Peel Quest (USA).nds" ] && bad "sd2: a game deleted from this card was downloaded to it again"
# a fresh second card (in, but no roms folder): the sync makes the folder and runs the sd2 scan (a stand-in here that
# writes the path as the sd2 script does), and the games go there
card rf
CODE=$(curl -s -XPOST "$U/test/paircode" | python3 -c 'import json,sys; print(json.load(sys.stdin)["code"])')
bs rf pair "$U" "$CODE" >/dev/null 2>&1 || bad "rf: pairing failed: $(st rf)"
printf 'homesync_roms_to = sd2\n' > "$OUT/rf/banana/settings.conf"
mkdir -p "$OUT/rf-sd2" "$OUT/rf/run/sd2"; : > "$OUT/rf/run/sd2/slot"
printf '#!/bin/sh\n[ -d "%s/roms" ] && echo "%s/roms" > "%s"\n' "$OUT/rf-sd2" "$OUT/rf-sd2" "$OUT/rf/run/sd2/roms" > "$OUT/sd2-scan"; chmod +x "$OUT/sd2-scan"
BANANA_SD2=$OUT/rf-sd2 BANANA_SD2_SCAN=$OUT/sd2-scan bs rf sync > "$OUT/sd2-fresh.log" 2>&1 || bad "sd2 fresh card: $(tail -3 "$OUT/sd2-fresh.log")"
[ -f "$OUT/rf-sd2/roms/gba/Banana Run (USA).gba" ] || bad "sd2: a fresh second card got no roms folder and games: $(grep -i 'second\|error' "$OUT/sd2-fresh.log" | head -2)"
# games copied by hand to the second card: adopted there (recorded as the second card's), so the next sync downloads
# nothing; and a state that recorded them as this card's (0.5.0.1-romm's bug) heals: adopted again, nothing downloaded
card rg
CODE=$(curl -s -XPOST "$U/test/paircode" | python3 -c 'import json,sys; print(json.load(sys.stdin)["code"])')
bs rg pair "$U" "$CODE" >/dev/null 2>&1 || bad "rg: pairing failed: $(st rg)"
printf 'homesync_roms_to = sd2\nhomesync.gba = off\nhomesync.psx = off\n' > "$OUT/rg/banana/settings.conf"
G2=$OUT/rg-sd2/roms; mkdir -p "$G2/nds" "$OUT/rg/run/sd2"; echo "$G2" > "$OUT/rg/run/sd2/roms"; : > "$OUT/rg/run/sd2/slot"
cp "$L/nds/Peel Quest (USA).nds" "$G2/nds/"
bs rg sync > "$OUT/sd2-adopt1.log" 2>&1 || bad "sd2 adopt: $(tail -3 "$OUT/sd2-adopt1.log")"
grep -q "0 to download" "$OUT/sd2-adopt1.log" || bad "sd2: a game on the second card was downloaded again: $(head -2 "$OUT/sd2-adopt1.log")"
grep -q '"nds/Peel Quest (USA).nds": "sd2"' "$OUT/rg/banana/romsync.json" || bad "sd2: an adopted game wasn't recorded as the second card's"
bs rg sync > "$OUT/sd2-adopt2.log" 2>&1
grep -q "0 to download" "$OUT/sd2-adopt2.log" || bad "sd2: the next sync downloaded an adopted game: $(head -2 "$OUT/sd2-adopt2.log")"
[ -e "$OUT/rg/roms/nds/Peel Quest (USA).nds" ] && bad "sd2: an adopted game came to this card"
python3 -I -c 'import json,sys; p=sys.argv[1]; d=json.load(open(p)); d["state"]["where"]={}; json.dump(d, open(p, "w"))' "$OUT/rg/banana/romsync.json"
bs rg sync > "$OUT/sd2-adopt3.log" 2>&1
grep -q "0 to download" "$OUT/sd2-adopt3.log" || bad "sd2: a state with the game recorded on this card downloaded it again: $(head -2 "$OUT/sd2-adopt3.log")"
grep -q '"nds/Peel Quest (USA).nds": "sd2"' "$OUT/rg/banana/romsync.json" || bad "sd2: the state wasn't healed"
# homesync_roms_to = sd2 on a handheld without a second slot (a card moved over): new games come to this card
card re
CODE=$(curl -s -XPOST "$U/test/paircode" | python3 -c 'import json,sys; print(json.load(sys.stdin)["code"])')
bs re pair "$U" "$CODE" >/dev/null 2>&1 || bad "re: pairing failed: $(st re)"
printf 'homesync_roms_to = sd2\n' > "$OUT/re/banana/settings.conf"
bs re sync > "$OUT/sd2-noslot.log" 2>&1 || bad "sd2 without a slot: $(tail -3 "$OUT/sd2-noslot.log")"
has "re/roms/gba/Banana Run (USA).gba"
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
: > "$OUT/rb/banana/homesync-get.txt"	# (what the killed sync was asked for: Get games' list doesn't go by Systems)
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
# games gone from the server (RomM): left on the card with their gamelist entries, as Get games' games are the
# owner's; a game asked for again that was changed on the card since: kept, not downloaded over
printf 'homesync.nds = off\n' > "$OUT/ra/banana/settings.conf"
curl -s -XPOST "$U/test/delete-rom" -d '{"id": 104}' >/dev/null      # Peel Quest (nds, skipped)
curl -s -XPOST "$U/test/delete-rom" -d '{"id": 102}' >/dev/null      # Banana Run (gba)
bs ra sync > "$OUT/romm-sync3.log" 2>&1
has "ra/roms/nds/Peel Quest (USA).nds"
has "ra/roms/gba/Banana Run (USA).gba"
grep -q 'Banana Run (USA).gba' "$G" || bad "a game gone from the server lost its gamelist entry"
grep -q '<name>My own</name>' "$G" || bad "the gamelist's own entry was lost"
printf 'homesync.nds = on\n' > "$OUT/ra/banana/settings.conf"
echo patched >> "$OUT/ra/roms/nds/Peel Quest (USA).nds"
bs ra get "nds/Peel Quest (USA).nds" >/dev/null
bs ra sync > "$OUT/romm-sync4.log" 2>&1
tail -c 8 "$OUT/ra/roms/nds/Peel Quest (USA).nds" | grep -q patched || bad "a ROM changed on the card was downloaded over: $(tail -3 "$OUT/romm-sync4.log")"
grep -q 'Peel Quest' "$OUT/ra/banana/homesync-get.txt" 2>/dev/null && bad "a game held back by a file of the owner's stayed on the list"
# an empty server listing never empties the card
rm -rf "$L/nds" "$L/gba" "$L/psx"; bs ra sync > "$OUT/romm-sync5.log" 2>&1
has "ra/roms/psx/Disc Game (USA).m3u"
bs ra status | grep -q "paired with RomM 5.4.1" || bad "status: $(bs ra status)"
# each sync's report kept on the card (banana/logs/sync.log, the one before as sync.log.1)
grep -q "^done: " "$OUT/ra/banana/logs/sync.log" 2>/dev/null || bad "no sync report in banana/logs/sync.log"
[ -f "$OUT/ra/banana/logs/sync.log.1" ] || bad "the sync before's report wasn't kept as sync.log.1"
# an automatic sync with Auto sync off says nothing and leaves the last report where it is
cp "$OUT/ra/banana/logs/sync.log" "$OUT/sync-log-before"
bs ra sync --auto quit >/dev/null 2>&1
cmp -s "$OUT/sync-log-before" "$OUT/ra/banana/logs/sync.log" || bad "an automatic sync that did nothing replaced the last sync's report"
bs ra forget; [ -f "$OUT/ra/banana/romsync.json" ] && bad "forget left the pairing"
# RomM on HTTPS (behind a reverse proxy), its certificate from the owner's own CA: found at the plain address, not
# trusted until homesync_ca names that CA's certificate, then paired and synced
LT=$OUT/romm-tls-lib; mkdir -p "$LT/gba"; head -c 3000 /dev/urandom > "$LT/gba/Secure Run (USA).gba"
openssl req -x509 -newkey rsa:2048 -nodes -keyout "$OUT/tls.key" -out "$OUT/tls.pem" -days 2 -subj /CN=127.0.0.1 \
    -addext "subjectAltName=IP:127.0.0.1" >/dev/null 2>&1 || bad "openssl made no certificate"
PT=$(port)
FAKE_ROMM_TLS="$OUT/tls.pem:$OUT/tls.key" python3 "$HERE/tests/fake-romm.py" "$PT" "$LT" 2> "$OUT/fake-romm-tls.log" & PIDS="$PIDS $!"
for i in $(seq 50); do curl -sk "https://127.0.0.1:$PT/api/heartbeat" >/dev/null && break; sleep 0.1; done
UT=https://127.0.0.1:$PT
card rt
TT=$(curl -sk -XPOST "$UT/test/token" -d '{}' | python3 -c 'import json,sys; print(json.load(sys.stdin)["token"])')
bs rt pair "$UT" "$TT" > "$OUT/romm-tls1.log" 2>&1 && bad "an HTTPS server nothing vouches for was trusted"
[ "$(st rt)" = "error the server's certificate isn't trusted (homesync_ca)" ] || bad "an untrusted certificate not said: $(st rt) $(cat "$OUT/romm-tls1.log")"
printf 'homesync_ca = banana/no-such-ca.pem\n' > "$OUT/rt/banana/settings.conf"
bs rt pair "$UT" "$TT" > "$OUT/romm-tls0.log" 2>&1 && bad "paired with a homesync_ca that isn't there"
[ "$(st rt)" = "error homesync_ca can't be read" ] || bad "a homesync_ca that isn't there not said: $(st rt) $(cat "$OUT/romm-tls0.log")"
cp "$OUT/tls.pem" "$OUT/rt/banana/home-ca.pem"; printf 'homesync_ca = banana/home-ca.pem\n' > "$OUT/rt/banana/settings.conf"
[ "$(bs rt probe "$UT")" = "romm $UT RomM 5.4.1" ] || bad "probe didn't find RomM on HTTPS: $(bs rt probe "$UT")"
bs rt pair "$UT" "$TT" > "$OUT/romm-tls2.log" 2>&1 || bad "pairing on HTTPS with homesync_ca: $(st rt) $(cat "$OUT/romm-tls2.log")"
grep -q "\"server\": \"$UT\"" "$OUT/rt/banana/romsync.json" || bad "the HTTPS address not kept: $(grep server "$OUT/rt/banana/romsync.json")"
bs rt sync > "$OUT/romm-tls3.log" 2>&1 || bad "a sync on HTTPS: $(tail -3 "$OUT/romm-tls3.log")"
cmp -s "$LT/gba/Secure Run (USA).gba" "$OUT/rt/roms/gba/Secure Run (USA).gba" || bad "a ROM over HTTPS: $(tail -3 "$OUT/romm-tls3.log")"
grep -q 'registered as RomM device\|error:' "$OUT/romm-tls3.log" && bad "the device on HTTPS: $(cat "$OUT/romm-tls3.log")"
grep -q 'certificate checks are off' "$OUT/romm-tls3.log" && bad "certificate checks said off while on"
# a self-signed certificate taken as it is, the owner's choice (homesync_tls = off): paired and synced, and each
# run says the checks are off
card ru; printf 'homesync_tls = off\n' > "$OUT/ru/banana/settings.conf"
bs ru pair "$UT" "$TT" > "$OUT/romm-tls4.log" 2>&1 || bad "pairing with homesync_tls off: $(st ru) $(cat "$OUT/romm-tls4.log")"
bs ru sync > "$OUT/romm-tls5.log" 2>&1 || bad "a sync with homesync_tls off: $(tail -3 "$OUT/romm-tls5.log")"
cmp -s "$LT/gba/Secure Run (USA).gba" "$OUT/ru/roms/gba/Secure Run (USA).gba" || bad "a ROM with homesync_tls off: $(tail -3 "$OUT/romm-tls5.log")"
grep -q 'certificate checks are off (homesync_tls)' "$OUT/romm-tls4.log" && grep -q 'certificate checks are off (homesync_tls)' "$OUT/romm-tls5.log" ||
    bad "certificate checks off not said"
# a RomM before 5.4 (whose slots and device records the other clients sync by): saves only come down, and the note
# says to update it; a development build counts as new (its saves by slot)
LV=$OUT/romm-old-lib; mkdir -p "$LV/gba"; printf 'gba rom down' > "$LV/gba/Down (USA).gba"; printf 'gba rom up' > "$LV/gba/Up (USA).gba"
for v in 5.3.1 development; do
    p=$(port); cv=rv-$v
    FAKE_ROMM_VERSION=$v python3 "$HERE/tests/fake-romm.py" "$p" "$LV" 2> "$OUT/fake-romm-$v.log" & PIDS="$PIDS $!"
    waitport "$p" /api/heartbeat || bad "the fake RomM $v didn't start"
    # (the plain probe, as Romhost's address finds a RomM, marks one before 5.4 as probe --romm does)
    case $v in 5.3.1) want="romm http://127.0.0.1:$p RomM 5.3.1 old" ;; *) want="romm http://127.0.0.1:$p RomM $v" ;; esac
    card "$cv"; [ "$(bs "$cv" probe "127.0.0.1:$p")" = "$want" ] || bad "RomM $v: probe: $(bs "$cv" probe "127.0.0.1:$p")"
    t=$(curl -s -XPOST "http://127.0.0.1:$p/test/token" -d '{}' | python3 -c 'import json,sys; print(json.load(sys.stdin)["token"])')
    card "$cv"; bs "$cv" pair "http://127.0.0.1:$p" "$t" >/dev/null 2>&1 && bs "$cv" sync >/dev/null 2>&1 || bad "RomM $v: pairing and the games: $(st "$cv")"
    sl=; [ $v = 5.3.1 ] || sl='"slot": "autosave", '
    # (a GBA EEPROM's 512 bytes, a size a GBA save comes in)
    curl -s -XPOST "http://127.0.0.1:$p/test/save" -d "{$sl\"rom_id\": 101, \"file_name\": \"Down (USA).srm\", \"data\": \"0102030405$(printf 'ff%.0s' $(seq 507))\"}" >/dev/null
    # (a GBA EEPROM's 512 bytes again: a save of a size its system's saves don't come in isn't sent)
    mkdir -p "$OUT/$cv/saves/gba"; { printf "this handheld's"; head -c 497 /dev/zero | tr '\0' x; } > "$OUT/$cv/saves/gba/Up (USA).srm"; cp "$OUT/$cv/saves/gba/Up (USA).srm" "$OUT/up-$v.srm"
    bs "$cv" sync --saves-only > "$OUT/romm-$v.log" 2>&1
    [ "$(head -c 5 "$OUT/$cv/saves/gba/Down (USA).srm" 2>/dev/null | od -An -tx1 | tr -d ' \n')" = 0102030405 ] || bad "RomM $v: the server's save didn't come down: $(cat "$OUT/romm-$v.log")"
    sent=$(curl -s -H "Authorization: Bearer $t" "http://127.0.0.1:$p/api/saves?rom_id=102" | python3 -c 'import json,sys; print(len(json.load(sys.stdin)))')
    case $v in
    5.3.1)
        [ "$sent" = 0 ] || bad "a save was sent to RomM 5.3.1"
        grep -q 'RomM 5.3.1 only gives saves: update RomM to 5.4 or newer' "$OUT/romm-$v.log" || bad "RomM 5.3.1: not said: $(cat "$OUT/romm-$v.log")"
        bs "$cv" status | grep -q "paired with RomM 5.3.1" || bad "status: $(bs "$cv" status)"
        # a save changed here and on the server (the card's never synced, the server's other bytes): held, both kept,
        # the card's as it is, the server's in the vault, nothing beside the save; never the newer by the files' times
        curl -s -XPOST "http://127.0.0.1:$p/test/save" -d "{\"rom_id\": 102, \"file_name\": \"Up (USA).srm\", \"data\": \"0a0b0c0d0e$(printf 'ff%.0s' $(seq 507))\"}" >/dev/null
        touch -d "@$(( $(date +%s) - 86400 ))" "$OUT/$cv/saves/gba/Up (USA).srm"
        bs "$cv" sync --saves-only > "$OUT/romm-$v-held.log" 2>&1
        cmp -s "$OUT/$cv/saves/gba/Up (USA).srm" "$OUT/up-$v.srm" && grep -q 'conflict, both kept: saves/gba/Up (USA).srm (the server.s in the vault)' "$OUT/romm-$v-held.log" &&
            [ -z "$(ls "$OUT/$cv/saves/gba" | grep '\.bak$')" ] && ls "$OUT/$cv/banana/save-vault/saves/gba/Up (USA)/" 2>/dev/null | grep -q '\.srm$' &&
            python3 -c 'import json,sys; h = json.load(open(sys.argv[1]))["held"]; sys.exit(0 if h["102/autosave"]["path"] == "saves/gba/Up (USA).srm" and h["102/autosave"]["vault"] else 1)' "$OUT/$cv/banana/romsync.json" ||
            bad "RomM 5.3.1: a save changed on both sides: $(cat "$OUT/romm-$v-held.log") $(ls "$OUT/$cv/saves/gba")"
        # (kept on the Saves page: this RomM takes no save from here, so the card's stays unsent, that version counts as seen,
        # and the next sync has nothing to hold)
        [ "$(bs "$cv" resolve 102/autosave card 2>/dev/null | tail -1)" = ok ] && cmp -s "$OUT/$cv/saves/gba/Up (USA).srm" "$OUT/up-$v.srm" ||
            bad "RomM 5.3.1: keeping this handheld's: $(bs "$cv" saves)"
        bs "$cv" sync --saves-only > "$OUT/romm-$v-held.log" 2>&1
        ! grep -q 'conflict, both kept' "$OUT/romm-$v-held.log" && ! bs "$cv" saves | grep -q '^held' || bad "RomM 5.3.1: a kept save held again: $(cat "$OUT/romm-$v-held.log")"
        # a save gone from the card since it synced: left as it is and said, never put back by itself (its last good copy
        # is in the vault); its record stays, so the next sync doesn't take the server's over nothing either
        rm "$OUT/$cv/saves/gba/Down (USA).srm"
        bs "$cv" sync --saves-only > "$OUT/romm-$v-gone.log" 2>&1; bs "$cv" sync --saves-only >> "$OUT/romm-$v-gone.log" 2>&1
        [ ! -e "$OUT/$cv/saves/gba/Down (USA).srm" ] && grep -q 'missing here since the last sync, left as it is: saves/gba/Down' "$OUT/romm-$v-gone.log" &&
            bs "$cv" saves | grep -q "^held	101/autosave	saves/gba/Down (USA).srm	missing here since the last sync" ||
            bad "RomM 5.3.1: a save gone from the card: $(cat "$OUT/romm-$v-gone.log") $(bs "$cv" saves)"
        # (blank instead of gone: noted the same, so the Saves page says so)
        head -c 8192 /dev/zero > "$OUT/$cv/saves/gba/Down (USA).srm"; bs "$cv" sync --saves-only > "$OUT/romm-$v-gone.log" 2>&1
        bs "$cv" saves | grep -q "^held	101/autosave	saves/gba/Down (USA).srm	blank here since the last sync" || bad "RomM 5.3.1: a blank save not noted: $(bs "$cv" saves)"
        rm "$OUT/$cv/saves/gba/Down (USA).srm"; bs "$cv" sync --saves-only > "$OUT/romm-$v-gone.log" 2>&1
        # (one game's save, here a game of a system the server has none of: this RomM has no slots to sync it by alone,
        # so every save of every system comes down, as after a game before)
        printf 'gba rom third' > "$LV/gba/Third (USA).gba"; cp "$LV/gba/Third (USA).gba" "$OUT/$cv/roms/gba/"
        curl -s -H "Authorization: Bearer $t" "http://127.0.0.1:$p/api/roms?limit=500" >/dev/null
        curl -s -XPOST "http://127.0.0.1:$p/test/save" -d "{\"rom_id\": 103, \"file_name\": \"Third (USA).srm\", \"data\": \"0605040302$(printf 'ff%.0s' $(seq 507))\"}" >/dev/null
        # (before a game starts, though, nothing: every save isn't worth the wait)
        echo 'homesync_auto = quit' >> "$OUT/$cv/banana/settings.conf"
        lists() { curl -s "http://127.0.0.1:$p/test/log" | grep -o 'GET /api/platforms' | wc -l; }
        n=$(lists)
        bs "$cv" sync --auto launch --rom "nds/Elsewhere (USA).nds" > "$OUT/romm-$v-rom.log" 2>&1
        [ -e "$OUT/$cv/saves/gba/Third (USA).srm" ] && bad "RomM 5.3.1: every save synced before a game: $(cat "$OUT/romm-$v-rom.log")"
        # (nor for a save left waiting to be sent, RomM since gone back to before 5.4)
        python3 -c 'import json, sys; c = json.load(open(sys.argv[1])); c["queue"] = {"101/autosave": {"md5": "x", "vault": "", "since": 1}}; json.dump(c, open(sys.argv[1], "w"))' "$OUT/$cv/banana/romsync.json"
        bs "$cv" sync --auto wifi > "$OUT/romm-$v-rom.log" 2>&1
        python3 -c 'import json, sys; c = json.load(open(sys.argv[1])); c["queue"] = {}; json.dump(c, open(sys.argv[1], "w"))' "$OUT/$cv/banana/romsync.json"
        sed -i '/^homesync_auto/d' "$OUT/$cv/banana/settings.conf"
        [ -e "$OUT/$cv/saves/gba/Third (USA).srm" ] && bad "RomM 5.3.1: every save synced for the Wi-Fi queue: $(cat "$OUT/romm-$v-rom.log")"
        [ "$(lists)" = "$n" ] || bad "RomM 5.3.1: its games listed before a game or for the Wi-Fi queue: $(cat "$OUT/romm-$v-rom.log")"
        # (a game started during a sync of every save: the saves not reached yet stay as they are)
        curl -s -XPOST "http://127.0.0.1:$p/test/fault" -d '{"path": "GET /api/saves$", "kind": "delay", "arg": 2}' >/dev/null
        bs "$cv" sync --rom "nds/Elsewhere (USA).nds" > "$OUT/romm-$v-rom.log" 2>&1 & _k=$!; sleep 1; sleep 30 & _g=$!; echo $_g > "$OUT/$cv/run/game.pid"; wait $_k; kill $_g; rm "$OUT/$cv/run/game.pid"
        [ -e "$OUT/$cv/saves/gba/Third (USA).srm" ] || ! grep -q 'a game started, the rest next time' "$OUT/romm-$v-rom.log" &&
            bad "RomM 5.3.1: a game started during a sync: $(cat "$OUT/romm-$v-rom.log")"
        bs "$cv" sync --rom "nds/Elsewhere (USA).nds" > "$OUT/romm-$v-rom.log" 2>&1
        grep -q "RomM 5.3.1: every save synced, not only nds/Elsewhere (USA).nds's" "$OUT/romm-$v-rom.log" &&
            [ "$(head -c 5 "$OUT/$cv/saves/gba/Third (USA).srm" 2>/dev/null | od -An -tx1 | tr -d ' \n')" = 0605040302 ] ||
            bad "RomM 5.3.1: --rom: $(cat "$OUT/romm-$v-rom.log")"
        rm "$LV/gba/Third (USA).gba"	# (the next RomM's ids as before)
        # (RomM updated, or downgraded, since the pairing: the name follows the heartbeat)
        sed -i 's/"RomM 5.3.1"/"RomM 5.2.0"/; s/"serverVersion": "5.3.1"/"serverVersion": "5.2.0"/' "$OUT/$cv/banana/romsync.json"
        bs "$cv" sync --saves-only >/dev/null 2>&1
        bs "$cv" status | grep -q "paired with RomM 5.3.1" || bad "the server's name didn't follow its version: $(bs "$cv" status)"
        sed -i 's/"RomM 5.3.1"/"RomM 5.2.0"/; s/"serverVersion": "5.3.1"/"serverVersion": "5.2.0"/' "$OUT/$cv/banana/romsync.json"
        bs "$cv" sync --rom "nds/Elsewhere (USA).nds" >/dev/null 2>&1
        bs "$cv" status | grep -q "paired with RomM 5.3.1" || bad "--rom: the server's name didn't follow its version: $(bs "$cv" status)" ;;
    *)
        [ "$sent" = 1 ] || bad "RomM $v: the save wasn't sent: $(cat "$OUT/romm-$v.log")"
        grep -q 'only gives saves' "$OUT/romm-$v.log" && bad "RomM $v taken for an old one" ;;
    esac
done

# (a pairing refused because a sync holds the lock: "error busy" over the menu's "pairing", never over the line the
# running sync wrote)
card pb; flock "$OUT/pb/run/sync.lock" sleep 6 & _f=$!; sleep 0.3
echo 'running 2 9 Saves|Banana Run (USA).srm' > "$OUT/pb/run/sync.status"
bs pb pair --romm http://127.0.0.1:1 >/dev/null 2>&1; r=$?
[ $r = 3 ] && [ "$(st pb)" = 'running 2 9 Saves|Banana Run (USA).srm' ] || bad "a pairing refused by a running sync wrote over its line: $r $(st pb)"
echo pairing > "$OUT/pb/run/sync.status"
bs pb pair --romm http://127.0.0.1:1 >/dev/null 2>&1
[ "$(st pb)" = 'error busy' ] || bad "a pairing refused by a running sync didn't say so: $(st pb)"
kill $_f 2>/dev/null; wait $_f 2>/dev/null

# ---------------------------------------------------------------- RomM on https (#109)
# behind a certificate of its own making (no CA on the handheld for it): https typed is used as it is, the check
# refuses it until Settings' Check certificate is Off, and http to the TLS port is a quick none, not a hang
TLS=$OUT/tls; mkdir -p "$TLS"
if openssl req -x509 -newkey rsa:2048 -nodes -subj /CN=romm.lan -days 2 -keyout "$TLS/key.pem" -out "$TLS/cert.pem" >/dev/null 2>&1; then
    TP=$(port)
    FAKE_ROMM_TLS="$TLS/cert.pem:$TLS/key.pem" python3 "$HERE/tests/fake-romm.py" "$TP" "$L" 2> "$OUT/romm-tls.log" & PIDS="$PIDS $!"
    for i in $(seq 50); do curl -sk "https://127.0.0.1:$TP/api/heartbeat" >/dev/null && break; sleep 0.1; done
    card ta
    [ "$(bs ta probe "https://127.0.0.1:$TP")" = "none tls" ] || bad "https: a certificate of no CA's was taken with Check certificate On, or not said as the certificate: $(bs ta probe "https://127.0.0.1:$TP")"
    # (said as the certificate, not the clock, though this card's run has had no time sync: its dates aren't what failed)
    r=$(bs ta probe --romm "https://127.0.0.1:$TP"); case $r in "fail tls"*) ;; *) bad "https: a certificate of no CA's, before a time sync: $r" ;; esac
    # one not valid until 2099, as a real one is to a clock the RTC left behind: the clock before a time sync, the
    # certificate after one (OpenSSL 3.0's req can't date a certificate ahead: ca can)
    FT=$TLS/future; mkdir -p "$FT/db"; : > "$FT/db/index"; echo 01 > "$FT/db/serial"
    printf '[ca]\ndefault_ca = d\n[d]\ndatabase = %s\nserial = %s\nnew_certs_dir = %s\ndefault_md = sha256\npolicy = p\n[p]\ncommonName = supplied\n' \
        "$FT/db/index" "$FT/db/serial" "$FT/db" > "$FT/ca.cnf"
    if openssl req -new -newkey rsa:2048 -nodes -subj /CN=romm.lan -keyout "$FT/key.pem" -out "$FT/req.pem" >/dev/null 2>&1 &&
        openssl ca -batch -notext -config "$FT/ca.cnf" -selfsign -keyfile "$FT/key.pem" -in "$FT/req.pem" \
            -startdate 20990101000000Z -enddate 20991231000000Z -out "$FT/cert.pem" >/dev/null 2>&1; then
        FP=$(port)
        FAKE_ROMM_TLS="$FT/cert.pem:$FT/key.pem" python3 "$HERE/tests/fake-romm.py" "$FP" "$L" 2>> "$OUT/romm-tls.log" & PIDS="$PIDS $!"
        for i in $(seq 50); do curl -sk "https://127.0.0.1:$FP/api/heartbeat" >/dev/null && break; sleep 0.1; done
        r=$(bs ta probe --romm "https://127.0.0.1:$FP"); case $r in "fail clock"*) ;; *) bad "https: a certificate not valid yet, before a time sync: $r" ;; esac
        mkdir -p "$OUT/ta/run"; : > "$OUT/ta/run/time-synced"
        r=$(bs ta probe --romm "https://127.0.0.1:$FP"); case $r in "fail tls"*) ;; *) bad "https: a certificate not valid yet, after a time sync: $r" ;; esac
        rm -f "$OUT/ta/run/time-synced"
    else
        bad "openssl couldn't make a certificate dated ahead"
    fi
    printf 'homesync_tls = off\n' > "$OUT/ta/banana/settings.conf"
    [ "$(bs ta probe "https://127.0.0.1:$TP")" = "romm https://127.0.0.1:$TP RomM 5.4.1" ] || bad "https probe: $(bs ta probe "https://127.0.0.1:$TP")"
    t0=$(date +%s); r=$(bs ta probe "127.0.0.1:$TP"); t1=$(date +%s)
    [ "$(echo "$r" | cut -d' ' -f1)" = none ] || bad "http to the TLS port: $r"
    [ $((t1 - t0)) -le 3 ] || bad "http to the TLS port took $((t1 - t0)) s"
    # a bare address: Romhost's 8092 first, https (443) when nothing answers on the http ports. 443 needs its own network
    # namespace (root in there, nobody's port 443 out here)
    if unshare -rn true 2>/dev/null; then
        export BIN OUT HERE TLS L
        unshare -rn sh -c '
            ip link set lo up
            BANANA_SYNC_NAME=t "$BIN" --root "$OUT/ta" --run "$OUT/ta/run" --status "$OUT/ta/run/sync.status" probe 127.0.0.1 > "$OUT/tls-none.txt"
            FAKE_ROMM_TLS="$TLS/cert.pem:$TLS/key.pem" python3 "$HERE/tests/fake-romm.py" 443 "$L" 2>> "$OUT/romm-tls.log" & A=$!
            for i in $(seq 50); do curl -sk https://127.0.0.1/api/heartbeat >/dev/null && break; sleep 0.1; done
            BANANA_SYNC_NAME=t "$BIN" --root "$OUT/ta" --run "$OUT/ta/run" --status "$OUT/ta/run/sync.status" probe 127.0.0.1 > "$OUT/tls-443.txt"
            python3 "$HERE/tests/fake-romm.py" 8092 "$L" 2>> "$OUT/romm-tls.log" & B=$!
            for i in $(seq 50); do curl -s http://127.0.0.1:8092/api/heartbeat >/dev/null && break; sleep 0.1; done
            BANANA_SYNC_NAME=t "$BIN" --root "$OUT/ta" --run "$OUT/ta/run" --status "$OUT/ta/run/sync.status" probe 127.0.0.1 > "$OUT/tls-8092.txt"
            kill $A $B'
        [ "$(cat "$OUT/tls-none.txt")" = "none  " ] || bad "a bare address with nothing there: $(cat "$OUT/tls-none.txt")"
        [ "$(cat "$OUT/tls-443.txt")" = "romm https://127.0.0.1 RomM 5.4.1" ] || bad "a bare address didn't find https on 443: $(cat "$OUT/tls-443.txt")"
        [ "$(cat "$OUT/tls-8092.txt")" = "romm http://127.0.0.1:8092 RomM 5.4.1" ] || bad "a bare address didn't try 8092 first: $(cat "$OUT/tls-8092.txt")"
    else
        echo "SKIP sync: a bare address on 443 (no unshare -rn here)"
    fi
else
    echo "SKIP sync: https (no openssl)"
fi

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
    export BANANA_RUN=$OUT/ui-run; rm -rf "$BANANA_RUN"; mkdir -p "$BANANA_RUN"
    # Settings > Network (the fifth tile) > its last row (Home server) > Server, not paired: A asks which kind, Romhost
    # (DOWN, A) looks on the LAN, finds the stand-in's server and asks for its code (typed: 1, START); then Sync now
    BANANA_SYNC="$HERE/tests/fake-sync.sh" FAKE_SYNC_LOG="$SL" "$HERE/banana-ui" --headless "$OUT/ui-" --size 640x480 --assets "$HERE/assets/rnds" \
        --splash "$HERE/assets/splash" --banana "$HERE/assets/banana" --systems "$HERE/../system/rootfs/etc/banana/systems.conf" \
        --roms "$FX/roms" --data "$FX/data" \
        --script "home 0; wait 2400; press start; press r; press r; press r; press a; press up; press up; shot hs-row; press a; shot hs-page; press a; shot hs-choose; press down; press a; wait 300; shot hs-code; press a; press start; sleep 300; wait 600; shot hs-paired; press down; shot hs-sync; press b; press b" \
        2> "$OUT/ui.log" || bad "banana-ui (Home server) exited $?"
    grep -q '^discover' "$SL" || bad "the Server row didn't look on the LAN: $(cat "$SL")"
    grep -q 'pair http://192.168.50.96:8092 .' "$SL" || bad "the code wasn't sent to pair: $(cat "$SL")"
    grep -q '"token": "rh1_test"' "$FX/data/romsync.json" 2>/dev/null || bad "no pairing written"
    # (a pairing turns Auto sync on, Leaving a game, when it is off, and says so)
    grep -q '^homesync_auto = quit' "$FX/data/settings.conf" || bad "pairing didn't turn Auto sync on: $(grep homesync_auto "$FX/data/settings.conf")"
    grep -q 'home server: Auto sync on' "$OUT/ui.log" || bad "Auto sync turned on not said: $(grep 'home server' "$OUT/ui.log")"
    for s in hs-row hs-page hs-choose hs-code hs-paired hs-sync; do
        [ -s "$OUT/ui-$s-top.png" ] || bad "no screenshot $s"
    done
    cmp -s "$OUT/ui-hs-row-top.png" "$OUT/ui-hs-page-top.png" && bad "A on Home server didn't open its page"
    cmp -s "$OUT/ui-hs-paired-top.png" "$OUT/ui-hs-sync-top.png" && bad "Romhost paired: the page's rows weren't made for it (no Sync now under Server)"
    # (no Romhost on the LAN, and RomM's address typed where Romhost's was asked for: RomM's pairing screen, its code
    # shown, and paired once it is approved)
    FXR=$OUT/fixture-rh; "$HERE/tests/fixture.sh" "$FXR" >/dev/null; printf 'wifi = on\n' >> "$FXR/data/settings.conf"; : > "$SL"
    BANANA_SYNC="$HERE/tests/fake-sync.sh" FAKE_SYNC_LOG="$SL" FAKE_SYNC_FIND=romm "$HERE/banana-ui" --headless "$OUT/ui-" --size 640x480 --assets "$HERE/assets/rnds" \
        --splash "$HERE/assets/splash" --banana "$HERE/assets/banana" --systems "$HERE/../system/rootfs/etc/banana/systems.conf" \
        --roms "$FXR/roms" --data "$FXR/data" \
        --script "home 0; wait 2400; press start; press r; press r; press r; press a; press up; press up; press a; press a; press down; press a; wait 300; press a; press start; sleep 300; wait 600; shot hs-rhromm; sleep 2600; wait 600" \
        2> "$OUT/ui-rhromm.log" || bad "banana-ui (RomM where Romhost's address was asked) exited $?"
    grep -q ' pair --romm http://' "$SL" || bad "RomM where Romhost's address was asked: no RomM pairing: $(cat "$SL")"
    grep -q '"token": "rmm_test"' "$FXR/data/romsync.json" 2>/dev/null || bad "RomM where Romhost's address was asked: not paired: $(grep 'home server' "$OUT/ui-rhromm.log")"
    grep -q 'home server: RomM 5.4.1 at http://' "$OUT/ui-rhromm.log" || bad "RomM where Romhost's address was asked: not taken as RomM's form takes it: $(grep 'home server' "$OUT/ui-rhromm.log")"
    grep -q '^homesync_launch = on' "$FXR/data/settings.conf" || bad "RomM where Romhost's address was asked: Sync before playing not turned on"
    # Home server > Saves: a game's conflict settled, then a kept copy restored; X deletes nothing
    : > "$SL"
    BANANA_SYNC="$HERE/tests/fake-sync.sh" FAKE_SYNC_LOG="$SL" "$HERE/banana-ui" --headless "$OUT/ui-" --size 640x480 --assets "$HERE/assets/rnds" \
        --splash "$HERE/assets/splash" --banana "$HERE/assets/banana" --systems "$HERE/../system/rootfs/etc/banana/systems.conf" \
        --roms "$FX/roms" --data "$FX/data" \
        --script "home 0; wait 2400; press start; press r; press r; press r; press a; press up; press up; press a; press down; press down; press down; press down; press down; press down; press down; press a; wait 300; shot sv-list; press a; wait 100; shot sv-game; press a; wait 300; press down; press down; press x; press x; wait 100; press a; wait 300; shot sv-restored" \
        2> "$OUT/ui-sv.log" || bad "banana-ui (Saves) exited $?"
    grep -q "resolve 5/autosave card" "$SL" || bad "Saves: this handheld's not chosen: $(cat "$SL")"
    [ "$(grep -c "^.* saves" "$SL")" -ge 2 ] || bad "Saves: the list not read again after a choice: $(cat "$SL")"
    grep -q "restore /v/20261101T100000Z-bb.srm" "$SL" && ! grep -q "drop " "$SL" || bad "Saves: a kept copy not restored, or X deleted one: $(cat "$SL")"
    cmp -s "$OUT/ui-sv-list-top.png" "$OUT/ui-sv-game-top.png" && bad "Saves: A didn't open a game"
    # (a game with nothing to choose: every button on it is safe. Two conflicts, the first settled: the next A opens the
    # list's, never settles the second's. Left during a choice and come back to: no freeze)
    SAVESCRIPT="home 0; wait 2400; press start; press r; press r; press r; press a; press up; press up; press a; press down; press down; press down; press down; press down; press down; press down; press a; wait 300"
    uis() { BANANA_SYNC="$HERE/tests/fake-sync.sh" FAKE_SYNC_LOG="$SL" FAKE_SYNC_SAVES=$1 "$HERE/banana-ui" --headless "$OUT/ui-" --size 640x480 --assets "$HERE/assets/rnds" --splash "$HERE/assets/splash" --banana "$HERE/assets/banana" --systems "$HERE/../system/rootfs/etc/banana/systems.conf" --roms "$FX/roms" --data "$FX/data" --script "$SAVESCRIPT; $2" 2> "$OUT/ui-sv.log" || bad "banana-ui (Saves, $1) exited $?"; }
    : > "$SL"; uis queued "press a; wait 100; press down; press a; press x; press r; press a; wait 100; press b; wait 100"
    grep -q "resolve\\|restore\\|drop" "$SL" && bad "Saves: a game with no actions ran: $(cat "$SL")"
    : > "$SL"; uis two "press a; wait 100; press a; wait 300; press a; wait 300"
    [ "$(grep -c "resolve 5/autosave card" "$SL")" -ge 1 ] && ! grep -q "resolve 6/autosave" "$SL" || bad "Saves: a settled game's page acted on another: $(cat "$SL")"
    : > "$SL"; uis twokept "press a; wait 100; press a; wait 300; press a; wait 300"
    grep -q "restore /v/a.srm" "$SL" && ! grep -q "resolve 6/autosave" "$SL" || bad "Saves: the game open changed under a choice: $(cat "$SL")"
    : > "$SL"; FAKE_SYNC_RESOLVE=slow uis conflict "press a; wait 100; press a; press b; press b; press a; wait 100; wait 2200; wait 300"
    grep -q "resolve 5/autosave card" "$SL" || bad "Saves: leaving during a choice: $(cat "$SL")"
    # (a save held for its format, DraStic's and BananaDS's apart, DraStic's unreadable, or gone here: a line of its own
    # for each, and A on the first three, which have nothing to choose, runs nothing)
    for k in format ds unread server; do
        : > "$SL"; uis $k "press a; wait 100; shot sv-$k; press a; wait 300"
        [ -s "$OUT/ui-sv-$k-top.png" ] || bad "no screenshot sv-$k"
        [ $k = server ] || ! grep -q "resolve\\|restore" "$SL" || bad "Saves: a $k note with nothing to choose ran: $(cat "$SL")"
    done
    grep -q "resolve 5/autosave server" "$SL" || bad "Saves: a save gone here, the server's not taken: $(cat "$SL")"
    for p in format:ds format:unread ds:unread unread:server ds:server; do
        cmp -s "$OUT/ui-sv-${p%:*}-top.png" "$OUT/ui-sv-${p#*:}-top.png" && bad "Saves: two kinds of note shown alike (${p%:*}, ${p#*:})"
    done
fi
# ---------------------------------------------------------------- RomM paired by its address (Network > Home server)
if [ -x "$HERE/banana-ui" ]; then
    FX=$OUT/fixture-r; "$HERE/tests/fixture.sh" "$FX" >/dev/null
    printf 'wifi = on\n' >> "$FX/data/settings.conf"
    SL=$OUT/fake-sync-r.log
    export BANANA_RUN=$OUT/ui-run-r
    HS="home 0; wait 2400; press start; press r; press r; press r; press a; press up; press up; press a; press a"
    # ur <name> <script> [VAR=value...]: the menu on a card never paired, from the Home server choice page, with the
    # stand-in; its log in $OUT/ui-<name>.log. UR_SYNC_PID: a sync's pid in sync.pid from the start
    ur() {
        n=$1 sc=$2; shift 2
        : > "$SL"; rm -rf "$BANANA_RUN" "$FX/data/romsync.json"; mkdir -p "$BANANA_RUN"
        [ -n "${UR_SYNC_PID:-}" ] && echo "$UR_SYNC_PID" > "$BANANA_RUN/sync.pid"
        env BANANA_SYNC="$HERE/tests/fake-sync.sh" FAKE_SYNC_LOG="$SL" "$@" "$HERE/banana-ui" --headless "$OUT/ui-" --size ${UR_SIZE:-640x480} --assets "$HERE/assets/rnds" \
            --splash "$HERE/assets/splash" --banana "$HERE/assets/banana" --systems "$HERE/../system/rootfs/etc/banana/systems.conf" \
            --roms "$FX/roms" --data "$FX/data" --script "$HS; $sc" 2> "$OUT/ui-$n.log" || bad "banana-ui (RomM, $n) exited $?"
    }
    rlog() { grep -q "$2" "$OUT/ui-$1.log" || bad "RomM ($1): no \"$2\" in the menu's log: $(grep 'home server' "$OUT/ui-$1.log")"; }
    # RomM, A: the form, on Address; its URL keyboard's https:// key (L, the symbols' first row), the rest typed with a
    # port and a folder: they go to their rows. START connects: the stand-in's RomM, then its code and QR code, approved
    # 2 s later; A then goes to Sync now on the paired page, with RomM's rows
    sed -i '/^homesync_auto/d; /^homesync_launch/d' "$FX/data/settings.conf"; printf 'homesync_auto = off\nhomesync_launch = off\n' >> "$FX/data/settings.conf"
    ur romm "shot hs-choose-romm; press a; shot hs-form0; press a; press l; press up; shot hs-urlkeys; press a; type romm.example.com:8443/romm; press start; shot hs-form; press start; sleep 400; wait 600; sleep 300; wait 600; shot hs-pair; sleep 2500; wait 600; wait 600; shot hs-paired-r; press a; shot hs-after"
    grep -q 'probe --romm https://romm.example.com:8443/romm$' "$SL" || bad "RomM: the form's address not asked as one URL: $(cat "$SL")"
    grep -q 'pair --romm https://romm.example.com:8443/romm$' "$SL" || bad "RomM: the device flow not started at the address checked: $(cat "$SL")"
    grep -q 'discover' "$SL" && bad "RomM: looked on the LAN for Romhost"
    # (the URL keyboard's L chip, tapped, opens the address keys as L does: http:// typed from them, over the form's https)
    ur urlchip "press a; press a; touch 1 345 442; wait 100; shot hs-urlchip; press up; press right; press a; type romm.example.com; press start; press start; sleep 300; wait 300" FAKE_SYNC_PAIR=hang
    grep -q 'probe --romm http://romm.example.com$' "$SL" || bad "RomM: the L chip's keys didn't type http://: $(cat "$SL")"
    rlog romm "home server: RomM 5.4.1 at https://romm.example.com:8443/romm"
    rlog romm "home server: code ABCD-EFGH, approve at https://romm.example.com:8443/romm/pair/device?user_code=ABCD-EFGH"
    rlog romm "home server: QR code"
    rlog romm "home server: paired with RomM 5.4.1"
    rlog romm "home server: Auto sync on (Leaving a game)"
    grep -q '^homesync_auto = quit' "$FX/data/settings.conf" || bad "RomM: pairing didn't turn Auto sync on: $(grep homesync_auto "$FX/data/settings.conf")"
    rlog romm "home server: Sync before playing on"
    grep -q '^homesync_launch = on' "$FX/data/settings.conf" || bad "RomM 5.4: pairing didn't turn Sync before playing on: $(grep homesync_launch "$FX/data/settings.conf")"
    # (a RomM before 5.4 has no sync before a game: its pairing turns Auto sync on and leaves Sync before playing off)
    sed -i '/^homesync_auto/d; /^homesync_launch/d' "$FX/data/settings.conf"; printf 'homesync_auto = off\nhomesync_launch = off\n' >> "$FX/data/settings.conf"
    ur romm-old "press a; press a; type romm.example.com; press start; press start; sleep 400; wait 600; sleep 2500; wait 600; wait 600" FAKE_SYNC_PROBE=old
    rlog romm-old "home server: paired with RomM 5.3.0"
    rlog romm-old "home server: Auto sync on (Leaving a game)"
    grep -q '^homesync_launch = off' "$FX/data/settings.conf" || bad "RomM 5.3: pairing turned Sync before playing on: $(grep homesync_launch "$FX/data/settings.conf")"
    # (and the game started next, with Sync before playing on, has its sync before it: the setting read from the card at
    # the game, not as the menu read it before the pairing)
    sed -i '/^homesync_auto/d; /^homesync_launch/d' "$FX/data/settings.conf"; printf 'homesync_auto = off\nhomesync_launch = on\n' >> "$FX/data/settings.conf"
    ur romm-launch "press a; press a; type romm.example.com; press start; press start; sleep 400; wait 600; sleep 2500; wait 600; wait 600; press start; wait 300; wifi online; launchpath $FX/roms/gba/Orchard Tactics.gba; sleep 300; wait 100"
    rlog romm-launch "home server: Auto sync on (Leaving a game)"
    grep -q 'sync --auto launch --rom' "$SL" || bad "RomM: the game after a pairing had no sync before it: $(cat "$SL")"
    sed -i '/^homesync_launch/d' "$FX/data/settings.conf"
    # (Device name, the paired page's row after Systems: typed, renamed in RomM by banana-sync rename, the new name on the row)
    env BANANA_SYNC="$HERE/tests/fake-sync.sh" FAKE_SYNC_LOG="$SL" "$HERE/banana-ui" --headless "$OUT/ui-" --size 640x480 --assets "$HERE/assets/rnds" \
        --splash "$HERE/assets/splash" --banana "$HERE/assets/banana" --systems "$HERE/../system/rootfs/etc/banana/systems.conf" \
        --roms "$FX/roms" --data "$FX/data" --script "home 0; wait 2400; press start; press r; press r; press r; press a; press up; press up; press a; press down; press down; press down; press down; press down; press down; press down; press down; shot hs-namerow; press a; press b; press b; press b; press b; press b; type Max DS; press start; sleep 300; wait 600; wait 600; shot hs-renamed" \
        2> "$OUT/ui-rename.log" || bad "banana-ui (rename) exited $?"
    grep -q ' rename Max DS$' "$SL" || bad "Device name: no rename asked: $(tail -3 "$SL")"
    grep -q '"name": "Max DS"' "$FX/data/romsync.json" || bad "Device name: the name not kept: $(cat "$FX/data/romsync.json")"
    rlog rename "home server: rename ok"
    for s in hs-namerow hs-renamed; do [ -s "$OUT/ui-$s-top.png" ] || bad "no screenshot $s"; done
    grep -q '"backend": "romm"' "$FX/data/romsync.json" 2>/dev/null || bad "RomM: no pairing written"
    for s in hs-choose-romm hs-form0 hs-urlkeys hs-form hs-pair hs-paired-r hs-after; do [ -s "$OUT/ui-$s-top.png" ] || bad "no screenshot $s"; done
    cmp -s "$OUT/ui-hs-form-bot.png" "$OUT/ui-hs-pair-bot.png" && bad "RomM: the pairing screen shows no QR code"
    cmp -s "$OUT/ui-hs-pair-top.png" "$OUT/ui-hs-paired-r-top.png" && bad "RomM: the approval not shown"
    # (the QR code read back, where a reader is at hand: the bottom panel's picture names the approval page)
    if command -v zbarimg >/dev/null 2>&1; then
        q=$(zbarimg -q --raw "$OUT/ui-hs-pair-bot.png" 2>/dev/null)
        [ "$q" = "https://romm.example.com:8443/romm/pair/device?user_code=ABCD-EFGH" ] || bad "RomM: the QR code reads \"$q\""
    else
        echo "SKIP sync: the QR code read back (no zbarimg)"
    fi
    # (and at 1024x768: the code and QR code as large as that panel allows)
    UR_SIZE=1024x768 ur romm-l "press a; press a; type romm.example.com; press start; press start; sleep 400; wait 600; sleep 300; wait 600; shot hs-pair-l; press b; wait 100"
    [ -s "$OUT/ui-hs-pair-l-bot.png" ] || bad "no screenshot hs-pair-l"
    # http:// redirected to https://: said, and Y takes the address it went to; then a RomM before 5.4 is said on the
    # pairing screen, and B cancels the pairing back to the form
    ur redirect "press a; press up; press right; press down; press a; type romm.example.com; press start; press start; sleep 300; wait 300; shot hs-redirect; press y; press start; sleep 300; wait 300; sleep 300; wait 300; shot hs-redirected; press b; wait 100; shot hs-cancelled" FAKE_SYNC_PROBE=redirect FAKE_SYNC_PAIR=hang
    grep -q 'probe --romm http://romm.example.com$' "$SL" && grep -q 'probe --romm https://romm.example.com$' "$SL" || bad "RomM: the redirect's address not taken: $(cat "$SL")"
    rlog redirect "home server: no RomM: redirect https://romm.example.com/api/heartbeat"
    rlog redirect "home server: code ABCD-EFGH"
    cmp -s "$OUT/ui-hs-redirected-top.png" "$OUT/ui-hs-cancelled-top.png" && bad "RomM: B on the pairing screen didn't go back"
    # Cloudflare Access's login in the way: said, nothing paired; a pairing code instead (X on the pairing screen)
    ur access "press a; press a; type romm.example.com; press start; press start; sleep 300; wait 300; shot hs-access" FAKE_SYNC_PROBE=access
    rlog access "home server: no RomM: access https://team.cloudflareaccess.com"
    grep -q 'pair' "$SL" && bad "RomM: paired past Cloudflare Access's login: $(cat "$SL")"
    # a sync running (a banana-sync's pid in sync.pid): the pairing says so and isn't started, and B leaves that sync be
    ln -sf "$(command -v sleep)" "$OUT/banana-sync"; "$OUT/banana-sync" 60 & BP=$!
    UR_SYNC_PID=$BP ur busy "press a; press a; type romm.example.com; press start; press start; sleep 400; wait 600; shot hs-busy; press b; wait 100"
    rlog busy "home server: pairing not started: a sync is running"
    grep -q 'pair --romm' "$SL" && bad "RomM: a pairing started under a running sync: $(cat "$SL")"
    kill -0 $BP 2>/dev/null || bad "RomM: B on the pairing screen stopped a sync that was running"
    kill $BP 2>/dev/null; wait $BP 2>/dev/null
    ur code "press a; press a; type romm.example.com; press start; press start; sleep 300; wait 300; press x; type ABCD-1234; press start; sleep 300; wait 600; shot hs-code-r" FAKE_SYNC_PAIR=hang
    grep -q "pair --romm https://romm.example.com 'ABCD-1234'\|pair --romm https://romm.example.com ABCD-1234" "$SL" || bad "RomM: the pairing code not sent: $(cat "$SL")"
    rlog code "home server: paired with RomM 5.4.1"
    # refused in RomM: said, A asks again
    ur deny "press a; press a; type romm.example.com; press start; press start; sleep 300; wait 300; sleep 2500; wait 600; wait 600; shot hs-denied; press a; sleep 300; wait 300" FAKE_SYNC_PAIR=deny
    rlog deny "home server: pairing failed: refused in RomM"
    [ "$(grep -c 'pair --romm' "$SL")" -ge 2 ] || bad "RomM: A after a refusal didn't ask again: $(cat "$SL")"
    # (the pairing's own probe refusing the certificate, the form's having passed: back on the form, where Y turns off
    # Check certificate and START connects again)
    sed -i '/^homesync_tls/d' "$FX/data/settings.conf"
    ur pairtls "press a; press a; type romm.example.com; press start; press start; sleep 300; wait 300; sleep 300; wait 300; shot hs-pairtls; press y; press start; sleep 300; wait 300" FAKE_SYNC_PAIR=tls
    rlog pairtls "home server: pairing failed: tls"
    grep -q '^homesync_tls = off' "$FX/data/settings.conf" || bad "RomM: Y after the pairing's certificate failure didn't turn Check certificate off: $(grep homesync_tls "$FX/data/settings.conf")"
    [ "$(grep -c 'probe --romm https://romm.example.com$' "$SL")" -ge 2 ] || bad "RomM: START on the form after the pairing's failure didn't connect again: $(cat "$SL")"
    sed -i '/^homesync_tls/d' "$FX/data/settings.conf"
    # (a pairing stopped that takes a moment to end, and Connect pressed at once: no wait on the press, the new pairing
    # started once the old one is gone, not said as a sync running)
    mkdir -p "$OUT/slow-bin"; ln -sf "$HERE/tests/fake-sync.sh" "$OUT/slow-bin/banana-sync"
    ur slowstop "press a; press a; type romm.example.com; press start; press start; sleep 300; wait 300; sleep 300; press b; wait 100; press start; sleep 300; wait 300; shot hs-stopping; sleep 1500; wait 300; sleep 300; wait 300" BANANA_SYNC="$OUT/slow-bin/banana-sync" FAKE_SYNC_PAIR=slowstop
    [ "$(grep -c 'pair --romm' "$SL")" = 2 ] || bad "RomM: a pairing asked for while the stopped one ended wasn't started after it: $(cat "$SL")"
    grep -q 'pairing not started: a sync is running' "$OUT/ui-slowstop.log" && bad "RomM: the pairing being stopped was said as a sync running"
    sleep 1; pkill -f "$OUT/slow-bin/banana-sync" 2>/dev/null
    # paired with RomM: Get games (the third row, after Server and Sync now) lists the server's games; A on one on the
    # card does nothing, A on one that isn't puts it on the download list and starts a sync, A again takes it back
    printf '{"backend": "romm", "server": "https://romm.example.com", "token": "rmm_test", "serverName": "RomM 5.4.1"}\n' > "$FX/data/romsync.json"
    GL=$FX/data/homesync-get.txt; rm -f "$GL"; : > "$SL"; rm -rf "$BANANA_RUN"; mkdir -p "$BANANA_RUN"
    # (A on Server, paired with RomM: its form on the paired address, as the row's help says, not the choice of server)
    env BANANA_SYNC="$HERE/tests/fake-sync.sh" FAKE_SYNC_LOG="$SL" "$HERE/banana-ui" --headless "$OUT/ui-" --size 640x480 --assets "$HERE/assets/rnds" \
        --splash "$HERE/assets/splash" --banana "$HERE/assets/banana" --systems "$HERE/../system/rootfs/etc/banana/systems.conf" \
        --roms "$FX/roms" --data "$FX/data" --script "$HS; wait 100; shot hs-server-romm" 2> "$OUT/ui-server-romm.log" || bad "banana-ui (Server, RomM paired) exited $?"
    grep -q "home server: RomM's form at https://romm.example.com$" "$OUT/ui-server-romm.log" || bad "Server, RomM paired: not its form on the paired address: $(grep 'home server' "$OUT/ui-server-romm.log")"
    # (a pairing that names no kind of server is Romhost's, as banana-sync takes it: its rows, Forget the last of them
    # after Check certificate; Forget, in the background, unpairs)
    printf '{"server": "http://192.168.50.96:8092", "token": "rh1_test", "serverName": "Test server"}\n' > "$FX/data/romsync.json"
    env BANANA_SYNC="$HERE/tests/fake-sync.sh" FAKE_SYNC_LOG="$SL" "$HERE/banana-ui" --headless "$OUT/ui-" --size 640x480 --assets "$HERE/assets/rnds" \
        --splash "$HERE/assets/splash" --banana "$HERE/assets/banana" --systems "$HERE/../system/rootfs/etc/banana/systems.conf" \
        --roms "$FX/roms" --data "$FX/data" --script "home 0; wait 2400; press start; press r; press r; press r; press a; press up; press up; press a; press down; press down; press down; press down; press down; press down; press down; press down; press down; shot hs-nobackend; press a; press a; sleep 300; wait 300" \
        2> "$OUT/ui-nobackend.log" || bad "banana-ui (no backend) exited $?"
    grep -q ' forget$' "$SL" || bad "a pairing naming no kind: Forget not the Romhost page's last row, or not run: $(cat "$SL")"
    grep -q 'home server: forgotten' "$OUT/ui-nobackend.log" || bad "Forget's end not taken: $(grep 'home server' "$OUT/ui-nobackend.log")"
    [ -e "$FX/data/romsync.json" ] && bad "Forget left the pairing"
    printf '{"backend": "romm", "server": "https://romm.example.com", "token": "rmm_test", "serverName": "RomM 5.4.1"}\n' > "$FX/data/romsync.json"
    : > "$SL"
    ug() { env BANANA_SYNC="$HERE/tests/fake-sync.sh" FAKE_SYNC_LOG="$SL" "$HERE/banana-ui" --headless "$OUT/ui-" --size 640x480 --assets "$HERE/assets/rnds" \
        --splash "$HERE/assets/splash" --banana "$HERE/assets/banana" --systems "$HERE/../system/rootfs/etc/banana/systems.conf" \
        --roms "$FX/roms" --data "$FX/data" --script "$1" 2> "$OUT/ui-get.log" || bad "banana-ui (Get games) exited $?"; }
    GET="home 0; wait 2400; press start; press r; press r; press r; press a; press up; press up; press a; press down; press down; shot hs-getrow; press a; sleep 300; wait 300"
    ug "$GET; shot hs-get0; press a; shot hs-get1; press a; wait 100; press down; press a; sleep 300; wait 1100; shot hs-get2"
    grep -qx 'gba/Pocket Hack.gba' "$GL" 2>/dev/null || bad "Get games: A didn't put the game on the list: $(cat "$GL" 2>&1)"
    grep -qx 'gba/Orchard Tactics.gba' "$GL" && bad "Get games: a game on the card was put on the list"
    grep -q ' sync --games-only$' "$SL" || bad "Get games: no sync of its own started: $(cat "$SL")"
    for s in hs-getrow hs-get0 hs-get1 hs-get2; do [ -s "$OUT/ui-$s-top.png" ] || bad "no screenshot $s"; done
    cmp -s "$OUT/ui-hs-get0-bot.png" "$OUT/ui-hs-get1-bot.png" && bad "Get games: A didn't open the system"
    # (a download that failed: Failed on its row, why under its title)
    printf '# x\ngba/Pocket Hack.gba\tchecksum mismatch\n' > "$FX/data/homesync-get-failed.txt"
    ug "$GET; press a; press down; sleep 300; wait 1100; shot hs-get-failed"
    cmp -s "$OUT/ui-hs-get2-top.png" "$OUT/ui-hs-get-failed-top.png" && bad "Get games: a failed download not shown"
    # (A on it tries it again: still on the list, its failure gone, Get games' own sync started; A then takes it back.
    # The stand-in's sync started above never says it's done: said here, or the menu rightly starts none beside it)
    echo 'done 0 0 0 0 0 0|games only' > "$BANANA_RUN/sync.status"
    : > "$SL"
    ug "$GET; press a; press down; press a; sleep 300; wait 1100"
    grep -qx 'gba/Pocket Hack.gba' "$GL" || bad "Get games: Try again took the game off the list: $(cat "$GL")"
    grep -q 'Pocket Hack' "$FX/data/homesync-get-failed.txt" 2>/dev/null && bad "Get games: tried again, the game kept its last failure: $(cat "$FX/data/homesync-get-failed.txt")"
    grep -q ' sync --games-only$' "$SL" || bad "Get games: Try again started no sync: $(cat "$SL")"
    ug "$GET; press a; press down; press a; sleep 300; wait 1100"
    grep -qx 'gba/Pocket Hack.gba' "$GL" && bad "Get games: A again didn't take the game back: $(cat "$GL")"
    rm -f "$FX/data/homesync-get-failed.txt"
    # (a sync that ends without having taken a game asked for on this visit, or said why: started again, once, the
    # page left at once for the Home server page)
    : > "$SL"; rm -f "$BANANA_RUN/sync.status"
    FAKE_SYNC_DONE=1 ug "$GET; press a; press down; press a; press b; press b; sleep 1500; wait 1100; sleep 1500; wait 1100; sleep 1500; wait 1100"
    n=$(grep -c ' sync --games-only$' "$SL")
    [ "$n" = 2 ] || bad "Get games: a sync ending with the game still waiting not started again once ($n syncs): $(cat "$SL")"
    # (and its line says it was Get games' own, for Sync now's report: the saves weren't looked at)
    grep -q '^done 0 0 0 0 0 0|games only$' "$BANANA_RUN/sync.status" || bad "Get games: its run's line doesn't say so: $(cat "$BANANA_RUN/sync.status")"
    : > "$GL"
    # (search, Y: from the systems every system's games, part of a title; B takes it away)
    ug "$GET; press y; type pock; press start; wait 300; shot hs-get-search; press a; sleep 300; wait 300; press b; press b; wait 100"
    grep -q 'get games: "pock" finds 1' "$OUT/ui-get.log" || bad "Get games: the search: $(grep 'get games' "$OUT/ui-get.log")"
    grep -qx 'gba/Pocket Hack.gba' "$GL" || bad "Get games: A on a game found didn't put it on the list: $(cat "$GL" 2>&1)"
    ug "$GET; press a; press y; type zzz; press start; wait 300; press down; press a; wait 100"
    grep -q 'get games: "zzz" finds 0' "$OUT/ui-get.log" || bad "Get games: a search finding nothing: $(grep 'get games' "$OUT/ui-get.log")"
    # (the server's list kept on the card: shown at once with nothing asked of the server, Start asks it once, another
    # server's isn't shown; X (Refresh) asks it again, from the systems or a system's games)
    GKF=$FX/data/homesync-server-games.tsv
    printf '# https://romm.example.com\t%s\ngba\tgba/Orchard Tactics.gba\tOrchard Tactics\t1024\t0\t1\ngba\tgba/Kept Only.gba\tKept Only\t2048\t0\t0\n' "$(($(date +%s) - 7200))" > "$GKF"
    : > "$SL"
    ug "$GET; shot hs-get-kept; press a; wait 300; press down; wait 100"
    grep -q ' games$' "$SL" && bad "Get games: a kept list, yet the server asked: $(cat "$SL")"
    grep -q 'get games: 2 games kept on the card' "$OUT/ui-get.log" || bad "Get games: the kept list not shown: $(grep 'get games' "$OUT/ui-get.log")"
    # (on the card as the cards say now, not as the kept list had it: a game kept as there, deleted since, is asked for
    # by A)
    printf '# https://romm.example.com\t%s\ngba\tgba/Kept Only.gba\tKept Only\t2048\t0\t1\ngba\tgba/Orchard Tactics.gba\tOrchard Tactics\t1024\t0\t1\n' "$(($(date +%s) - 7200))" > "$GKF"
    : > "$GL"; : > "$SL"
    ug "$GET; press a; wait 300; press a; sleep 300; wait 300"
    grep -qx 'gba/Kept Only.gba' "$GL" || bad "Get games: a game gone from the card since the list was kept couldn't be asked for: $(cat "$GL")"
    : > "$GL"
    : > "$SL"
    ug "$GET; press x; sleep 300; wait 600; shot hs-get-refreshed; wait 100"
    [ "$(grep -c ' games$' "$SL")" = 1 ] || bad "Get games: X didn't ask the server once: $(cat "$SL")"
    grep -q 'get games: refreshed, 2 games' "$OUT/ui-get.log" || bad "Get games: X's list not taken: $(grep 'get games' "$OUT/ui-get.log")"
    : > "$SL"
    ug "$GET; press a; wait 300; press down; press x; sleep 300; wait 600; shot hs-get-refreshed1; wait 100"
    [ "$(grep -c ' games$' "$SL")" = 1 ] || bad "Get games: X in a system's games didn't ask the server once: $(cat "$SL")"
    printf '# https://other.example.com\t1\ngba\tgba/Kept Only.gba\tKept Only\t2048\t0\t0\n' > "$GKF"; : > "$SL"
    ug "$GET; wait 100"
    [ "$(grep -c ' games$' "$SL")" = 1 ] || bad "Get games: another server's kept list was shown: $(cat "$SL")"
    rm -f "$GKF"
    # (saves to settle: counted on Manage saves' row and on Network's Home server row, nothing said with none)
    NET="home 0; wait 2400; press start; press r; press r; press r; press a; press up; press up; wait 100"
    ug "$NET; shot net-settle0; press a; press down; press down; press down; press down; press down; press down; wait 100; shot hs-settle0"
    cp "$FX/data/romsync.json" "$OUT/romsync-plain.json"
    printf '{"backend": "romm", "server": "https://romm.example.com", "token": "rmm_test", "serverName": "RomM 5.4.1", "conflicts": {"5/autosave": {"path": "saves/gba/Orchard Tactics.srm", "why": "both changed"}}, "held": {"6/autosave": {"path": "saves/gba/Aaa.srm", "why": "a format this handheld doesn'"'"'t read"}}}\n' > "$FX/data/romsync.json"
    ug "$NET; shot net-settle1; press a; press down; press down; press down; press down; press down; press down; wait 100; shot hs-settle1"
    cp "$OUT/romsync-plain.json" "$FX/data/romsync.json"
    cmp -s "$OUT/ui-net-settle0-bot.png" "$OUT/ui-net-settle1-bot.png" && bad "Network's Home server row: saves to settle not counted"
    cmp -s "$OUT/ui-hs-settle0-bot.png" "$OUT/ui-hs-settle1-bot.png" && bad "Manage saves' row: saves to settle not counted"
    # (Sync now as Wi-Fi comes back, pressed as the lid opens: it waits, and starts once Wi-Fi has an address)
    HSN="home 0; wait 2400; press start; press r; press r; press r; press a; press up; press up; press a"
    rm -f "$BANANA_RUN/sync.status"; : > "$SL"
    ug "$HSN; wifi coming; press down; press a; sleep 1000; wait 100; shot hs-wifiwait"
    grep -q 'home server: the sync waits for Wi-Fi' "$OUT/ui-get.log" || bad "Sync now with Wi-Fi coming back didn't wait: $(grep 'home server' "$OUT/ui-get.log")"
    grep -q ' sync$' "$SL" && bad "Sync now started before Wi-Fi was back: $(cat "$SL")"
    rm -f "$BANANA_RUN/sync.status"; : > "$SL"
    ug "$HSN; wifi coming; press down; press a; sleep 500; wait 100; wifi online; sleep 500; wait 100"
    grep -q 'home server: the sync starts' "$OUT/ui-get.log" && [ "$(grep -c ' sync$' "$SL")" = 1 ] || bad "Sync now didn't start once Wi-Fi was back: $(cat "$SL")"
    rm -f "$BANANA_RUN/sync.status"
    # (the game page, X on a game in the library: Save sync, paired with RomM, after the DS rows, name, sort and art)
    LC=$FX/data/homesync-local.txt; rm -f "$LC"
    ug "lib 0 0; press x; wait 300; press down; press down; press down; press down; press down; press down; press down; press down; press down; shot pg-sync; press right; wait 300; press b; wait 300"
    grep -qx 'nds/Banana Kart DS.nds' "$LC" 2>/dev/null || bad "Save sync: the game's line not written: $(cat "$LC" 2>&1)"
    ug "lib 0 0; press x; wait 300; press down; press down; press down; press down; press down; press down; press down; press down; press down; press left; wait 300; press b; wait 300"
    grep -q 'Banana Kart' "$LC" && bad "Save sync on again left the line: $(cat "$LC")"
    rm -f "$FX/data/romsync.json"
    unset BANANA_RUN
fi
# ---------------------------------------------------------------- the sync before a game
if [ -x "$HERE/banana-ui" ]; then
    FX=$OUT/fixture-g; "$HERE/tests/fixture.sh" "$FX" >/dev/null
    printf 'homesync_auto = quit\nhomesync_launch = on\n' >> "$FX/data/settings.conf"
    printf '{"backend": "romm", "server": "http://x", "token": "t"}\n' > "$FX/data/romsync.json"
    ROM="$FX/roms/gba/Orchard Tactics.gba"
    SL=$OUT/game-sync.log; PX=$OUT/g-; RUNG=${PX}run; mkdir -p "$RUNG"
    # gs <how> <script>: a run; its log in $UL, what banana-sync was called with in $SL
    UL=$OUT/g-ui.log
    gs() {
        : > "$SL"
        BANANA_SYNC=${TOOL:-$HERE/tests/fake-sync.sh} FAKE_SYNC_LOG="$SL" FAKE_SYNC_LAUNCH=$1 "$HERE/banana-ui" --headless "$PX" --size 640x480 --assets "$HERE/assets/rnds" \
            --splash "$HERE/assets/splash" --banana "$HERE/assets/banana" --systems "$HERE/../system/rootfs/etc/banana/systems.conf" \
            --roms "$FX/roms" --data "$FX/data" --script "home 0; $2" 2> "$UL" || bad "banana-ui (sync before a game) exited $?"
    }
    has() { grep -q "$1" "$UL" || bad "$2: no \"$1\": $(grep -a 'sync before\|game starts' "$UL")"; }
    hasnt() { grep -q "$1" "$UL" && bad "$2: \"$1\": $(grep -a 'sync before\|game starts' "$UL")"; }
    called() { [ -s "$SL" ] && bad "$1: banana-sync was called: $(cat "$SL")"; }
    # online and quick: the exact call, the game after it, no pop-up
    gs answer "wifi online; launchpath $ROM; sleep 300; sleep 100; shot g-quick; wait 100"
    [ "$(cat "$SL")" = "--run $RUNG sync --auto launch --rom $ROM" ] || bad "the sync before a game's call: $(cat "$SL")"
    has "sync before the game: done" "a quick sync"; has "game starts: $ROM" "a quick sync"
    # the first answer never comes: stopped at 5 s, not before; said; then the game
    gs silent "wifi online; launchpath $ROM; sleep 200; wait 450; shot g-plain; wait 4000; sleep 100"
    hasnt "no answer" "a server silent for under 5 s"; hasnt "game starts" "a server silent for under 5 s"
    gs silent "wifi online; launchpath $ROM; sleep 200; wait 500; shot g-sync; wait 4500; shot g-not; sleep 100; wait 1000; wait 600; wait 100"
    has "sync before the game: no answer" "a silent server"; has "sync before the game: Not synced yet" "a silent server"; has "game starts: $ROM" "a silent server"
    cmp -s "$OUT/g-g-sync-top.png" "$OUT/g-g-not-top.png" && bad "the notice looks like the wait"
    # the saves held by another sync (the one after the last game, still placing): waited out, and the server then has
    # its 5 s for a first answer from there, so no answer at all is said 5 s after the hold, not 5 s into it or at the
    # cap. (The fake holds them 8 s of real time, and the menu's clock moves with every sleep and wait, a step of the
    # wait at each: the hold is slept through a second at a time to 5.3 s, each step seeing it, the menu's drawing
    # between steps costing real time but no clock; one sleep of 3.5 s more is past the hold however slow the drawing;
    # then 4.9 s and 0.2 s of wait)
    HOLD="sleep 300; sleep 1000; sleep 1000; sleep 1000; sleep 1000; sleep 1000; sleep 3500"
    gs held "wifi online; launchpath $ROM; $HOLD; wait 1400"
    hasnt "no answer" "the saves held, then silence, 4.9 s after the hold"; hasnt "game starts" "the saves held, then silence, 4.9 s after the hold"
    gs held "wifi online; launchpath $ROM; $HOLD; wait 1400; wait 200; wait 1600; wait 100"
    has "sync before the game: no answer" "the saves held, then silence"; hasnt "stopped at the cap" "the saves held, then silence"; has "game starts: $ROM" "the saves held, then silence"
    # (held throughout: the cap, 20 s from the wait's start, ends it)
    gs heldlong "wifi online; launchpath $ROM; sleep 300; wait 19000"
    hasnt "game starts" "the saves held, at 19.3 s"
    gs heldlong "wifi online; launchpath $ROM; sleep 300; wait 19000; wait 1000; wait 1600; wait 100"
    has "sync before the game: no answer" "the saves held past the cap"; has "game starts: $ROM" "the saves held past the cap"
    # answered, then slow: past 5 s it goes on, the cap at 20 s stops it
    gs hang "wifi online; launchpath $ROM; sleep 300; wait 6000; sleep 100; wait 10000; sleep 100"
    hasnt "stopped at the cap" "a transfer before the cap"
    gs hang "wifi online; launchpath $ROM; sleep 300; wait 6000; sleep 100; wait 14000; sleep 100; wait 1600; wait 100"
    has "stopped at the cap" "a transfer at the cap"; has "Not synced yet" "a transfer at the cap"; has "game starts" "a transfer at the cap"
    # B plays now: during the sync, and during the wait for Wi-Fi
    gs silent "wifi online; launchpath $ROM; sleep 200; press b; wait 100"
    has "B, played now" "B while syncing"; has "game starts: $ROM" "B while syncing"; hasnt "Not synced yet" "B while syncing"
    ls "$RUNG" | grep -q '^sync-' && bad "a stopped sync's files left: $(ls "$RUNG")"
    gs silent "wifi coming; launchpath $ROM; wait 500; shot g-wifi; press b; wait 100"
    # (a mark an earlier game's run left says nothing of this one: a B before this sync starts asks nothing)
    : > "$RUNG/launch.older"
    gs silent "wifi coming; launchpath $ROM; wait 500; press b; wait 100"
    hasnt "newer save couldn't be fetched" "a stale mark, B during the wait for Wi-Fi"; has "game starts: $ROM" "a stale mark, B during the wait for Wi-Fi"
    gs silent "wifi coming; launchpath $ROM; wait 500; shot g-wifi; press b; wait 100"
    has "B, played now" "B on Wi-Fi"; has "game starts: $ROM" "B on Wi-Fi"; called "B on Wi-Fi"
    cmp -s "$OUT/g-g-wifi-top.png" "$OUT/g-g-sync-top.png" && bad "the wait for Wi-Fi looks like the sync"
    # the pop-up leaves with the wait: the menu as it was
    gs silent "wait 3000; shot g-before; wifi coming; launchpath $ROM; wait 500; press b; wait 100; shot g-after"
    has "game starts: $ROM" "the wait over"
    cmp -s "$OUT/g-g-before-top.png" "$OUT/g-g-after-top.png" || bad "the pop-up stayed after the wait"
    # A on a game in its list, the way a game really starts
    gs answer "wifi online; lib 1 0; press a; wait 3000; wait 1; sleep 300; wait 100"
    has "^banana-ui: launch " "A on a game"; has "sync before the game: done" "A on a game"; has "game starts: " "A on a game"
    # the signs of Wi-Fi coming up not quite meeting: still waiting, and the sync starts when the address comes
    gs answer "wifi coming; launchpath $ROM; wait 3000; wifi none; wait 3000; wifi online; wait 100; sleep 300; wait 100"
    has "sync before the game: done" "a gap in Wi-Fi coming up"; hasnt "no Wi-Fi" "a gap in Wi-Fi coming up"
    # a conflict held for another reason than the two sides (nothing to choose): a notice, which a press ends
    FAKE_SYNC_SAVES=none gs conflict "wifi online; launchpath $ROM; sleep 300; wait 100; shot g-conflict; press a; shot g-x"
    has "game starts: $ROM" "a press on the notice"
    # a conflict: asked before the game, and it waits for the answer however long that takes. B leaves it held
    gs conflict "wifi online; launchpath $ROM; sleep 300; wait 25000; shot g-ask; press y; wait 100"
    has "asking about a conflict" "a conflict"; hasnt "game starts" "a conflict unanswered"
    gs conflict "wifi online; launchpath $ROM; sleep 300; wait 100; press b; wait 100"
    has "decided later" "a conflict put off"; has "game starts: $ROM" "a conflict put off"; grep -q resolve "$SL" && bad "a conflict put off was settled: $(cat "$SL")"
    # settled there, then the game
    gs conflict "wifi online; launchpath $ROM; sleep 300; wait 100; press a; sleep 300; wait 100"
    grep -q "resolve 5/autosave card" "$SL" || bad "the prompt's A: $(cat "$SL")"; has "settled" "the prompt's A"; has "game starts: $ROM" "the prompt's A"
    gs conflict "wifi online; launchpath $ROM; sleep 300; wait 100; press x; sleep 300; wait 100"
    grep -q "resolve 5/autosave server" "$SL" || bad "the prompt's X: $(cat "$SL")"
    # (it can't be settled: asked, the conflict still held: A plays the card's save, B goes back and no game starts)
    FAKE_SYNC_RESOLVE=fail gs conflict "wifi online; launchpath $ROM; sleep 300; wait 100; press a; sleep 300; wait 100; shot g-unsettled; press a; wait 100"
    has "Not settled" "a conflict not settled"; has "not settled" "a conflict not settled"; has "game starts: $ROM" "a conflict not settled"
    FAKE_SYNC_RESOLVE=fail gs conflict "wifi online; launchpath $ROM; sleep 300; wait 100; press a; sleep 300; wait 100; press b; wait 100"
    has "Not settled" "back from a conflict not settled"; has "game not started: $ROM" "back from a conflict not settled"; hasnt "game starts" "back from a conflict not settled"
    # (a choice runs its course: B does nothing, and the cap ends it, which asks the same)
    FAKE_SYNC_RESOLVE=hang gs conflict "wifi online; launchpath $ROM; sleep 300; wait 100; press a; wait 100; press b; wait 100; wait 20000; sleep 100; wait 1600; wait 100; press a; wait 100"
    has "Not settled at the cap" "a choice at the cap"; has "game starts: $ROM" "a choice at the cap"; hasnt "played now" "B during a choice"
    # a save gone or blank here since the last sync, the server's there to take back: asked; A takes it, B plays without
    FAKE_SYNC_SAVES=server gs conflict "wifi online; launchpath $ROM; sleep 300; wait 100; shot g-gone; press a; sleep 300; wait 100"
    grep -q "resolve 5/autosave server" "$SL" || bad "the question of a save gone here, A: $(cat "$SL")"; has "game starts: $ROM" "a save gone here, taken back"
    FAKE_SYNC_SAVES=server gs conflict "wifi online; launchpath $ROM; sleep 300; wait 100; press b; wait 100"
    has "decided later" "a save gone here, played without"; has "game starts: $ROM" "a save gone here, played without"; grep -q resolve "$SL" && bad "a save gone here, B: settled: $(cat "$SL")"
    # (decided later: asked again at the next launch)
    gs conflict "wifi online; launchpath $ROM; sleep 300; wait 100; press b; wait 100; launchpath $ROM; sleep 300; wait 100; press b; wait 100"
    [ "$(grep -c 'asking about a conflict' "$UL")" = 2 ] || bad "a conflict put off wasn't asked again: $(grep -a 'sync before' "$UL")"
    # a newer save still coming at the cap: asked too; and at a B, which doesn't play over it unasked
    gs olderhang "wifi online; launchpath $ROM; sleep 300; wait 6000; sleep 100; wait 14000; sleep 100; wait 100; press a; wait 100"
    has "couldn't be fetched" "a stop at the cap with a newer save"; has "game starts: $ROM" "a stop at the cap with a newer save"
    gs olderhang "wifi online; launchpath $ROM; sleep 300; wait 100; press b; wait 100"
    has "couldn't be fetched" "B with a newer save coming"; hasnt "game starts" "B with a newer save coming"
    gs olderhang "wifi online; launchpath $ROM; sleep 300; wait 100; press b; wait 100; press a; wait 100"
    has "game starts: $ROM" "B with a newer save coming, then A"
    # a newer save on the server that didn't arrive: asked. A plays the older, B goes back and no game starts
    gs older "wifi online; launchpath $ROM; sleep 300; wait 100; press a; wait 100"
    has "played on this handheld's older save" "an older save"; has "game starts: $ROM" "an older save"
    gs older "wifi online; launchpath $ROM; sleep 300; wait 100; press b; wait 100"
    has "game not started: $ROM" "back from an older save"; hasnt "game starts" "back from an older save"
    # no banana-sync: no wait
    TOOL=$OUT/none gs answer "wifi online; launchpath $ROM; wait 100"
    has "no banana-sync" "no banana-sync"; has "game starts: $ROM" "no banana-sync"
    # another game's resume marker left behind: this one still syncs
    printf '%s\n' "$FX/roms/gba/Other.gba" > "$RUNG/resume-once"
    gs answer "wifi online; launchpath $ROM; sleep 300; wait 100"
    has "sync before the game: done" "a stale resume marker"
    # Wi-Fi: off, nothing; not coming in 8 s, the game; there part way, the sync starts
    gs answer "wifi none; launchpath $ROM; wait 100"
    has "no Wi-Fi" "Wi-Fi off"; has "game starts: $ROM" "Wi-Fi off"; called "Wi-Fi off"
    gs answer "wifi coming; launchpath $ROM; wait 7000; wait 2000; wait 1600; wait 100"
    has "Not synced yet" "Wi-Fi never came"; has "game starts: $ROM" "Wi-Fi never came"; called "Wi-Fi never came"
    gs answer "wifi coming; launchpath $ROM; wait 3000; wifi online; wait 100; sleep 300; wait 100"
    has "sync before the game: done" "Wi-Fi came"
    # a conflict and a failure are said, and the game starts on the card's save
    FAKE_SYNC_SAVES=none gs conflict "wifi online; launchpath $ROM; sleep 300; wait 1600; wait 100"
    has "Save conflict" "a conflict"; has "game starts: $ROM" "a conflict"
    gs fail "wifi online; launchpath $ROM; sleep 300; wait 1600; wait 100"
    has "Not synced yet" "a failed sync"
    # the game started again after a power-off: nothing waits for it
    printf 'rom=%s\nemulator=x\n' "$ROM" > "$FX/data/resume-boot"
    gs answer "wifi online; resumeboot; wait 100"
    has "game starts: $ROM" "the resume launch"; called "the resume launch"
    # the game's end: the exact call
    gs answer "wifi none; launchpath $ROM; wait 100; gameend; sleep 300"
    # (its lines to a file of this game end's own, so an earlier game's sync still going isn't read as this one's)
    [ "$(sed -E 's/gameend-[0-9]+\.status/gameend-N.status/' "$SL")" = "--run $RUNG --report $RUNG/gameend-N.status sync --auto quit --rom $ROM" ] ||
        bad "the sync after a game's call: $(cat "$SL")"
    # what it did, said in the banner: sent, nothing to send, a conflict, the server out of reach; a slow one says it
    # is syncing first, and its outcome after
    for q in sent:"Save synced" same:"Save up to date" conflict:"Save conflict" note:"Save up to date: Orchard Tactics: 1 state from the server" \
        away:"Save not synced: Orchard Tactics: the server's name wasn't found (no connection yet?); kept to send with the next sync"; do
        FAKE_SYNC_QUIT=${q%%:*} gs answer "wifi none; launchpath $ROM; wait 100; gameend; sleep 300; wait 600; shot ge-${q%%:*}"
        has "notice: ${q#*:}" "the sync after a game, ${q%%:*}"
    done
    FAKE_SYNC_QUIT=slow gs answer "wifi none; launchpath $ROM; wait 100; gameend; sleep 1200; sleep 300; shot ge-running; sleep 1500; sleep 600"
    has "notice: Syncing the save: Orchard Tactics" "a slow sync after a game"; has "notice: Save synced: Orchard Tactics" "a slow sync after a game's outcome"
    # a game whose saves stay here: nothing said; a run gone with its line left: let go, nothing more said
    FAKE_SYNC_QUIT=local gs answer "wifi none; launchpath $ROM; wait 100; gameend; sleep 300; wait 600"
    hasnt "notice:" "the sync after a game kept here"
    FAKE_SYNC_QUIT=stuck gs answer "wifi none; launchpath $ROM; wait 100; gameend; sleep 300; wait 1200; wait 600"
    has "the sync said nothing more" "a sync after a game gone"; hasnt "notice:" "a sync after a game gone"
    # (a line as old from a run still holding save.lock, as a long upload is: watched on, its outcome said)
    FAKE_SYNC_QUIT=held gs answer "wifi none; launchpath $ROM; wait 100; gameend; sleep 300; wait 1200; sleep 2500; wait 600"
    hasnt "the sync said nothing more" "a long sync after a game"; has "notice: Save synced: Orchard Tactics" "a long sync after a game's outcome"
    # (two game ends, the first's sync still going at the second: each file gone once its run is over, none left in
    # /run, which is memory)
    rm -f "$RUNG"/gameend-*.status
    FAKE_SYNC_QUIT=held gs answer "wifi none; launchpath $ROM; wait 100; gameend; sleep 300; launchpath $ROM; wait 100; gameend; sleep 300; wait 1200; sleep 2500; wait 600; sleep 2500; wait 600"
    [ -z "$(ls "$RUNG" | grep '^gameend-')" ] || bad "game ends' status files left: $(ls "$RUNG" | grep '^gameend-' | tr '\n' ' ')"
    # (a DS game's end: nds-saves told what its saves hold now, --played, beside the sync; not for a GBA game, above)
    NDSROM="$FX/roms/nds/Peel Quest.nds"
    BANANA_NDS_SAVES=$HERE/tests/fake-sync.sh gs answer "wifi none; launchpath $NDSROM; wait 100; gameend; sleep 300"
    grep -qx -- "$NDSROM --played" "$SL" && sed -E 's/gameend-[0-9]+\.status/gameend-N.status/' "$SL" | grep -qxF -- "--run $RUNG --report $RUNG/gameend-N.status sync --auto quit --rom $NDSROM" ||
        bad "a DS game's end: $(cat "$SL")"
fi
[ $fail = 0 ] && echo "banana-sync: all checks passed"
exit $fail

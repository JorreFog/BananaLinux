#!/bin/sh
# RomM's slotted saves (RomM 5.4: versions per slot, the devices' sync records, negotiation), which BananaOS's save
# sync is built on, against tests/fake-romm.py:
#   the stand-in itself   tests/romm-conformance.py, the cases measured on a real RomM 5.4.1, against the stand-in (and
#                         against a real RomM when $ROMM_URL and $ROMM_TOKENS, a JSON of three client tokens, are set)
#   its faults            /test/fault's switches and /test/strict's tripwires, each doing what it says
#   other clients         tests/romm-peers.py, a Grout-style client and one writing as BananaOS's sync will, sharing
#                         the slots on a stand-in of their own
#   the real card         tests/sync-card.sh's helpers, a card laid out as a handheld's /data really is
#   the card's safety     banana-sync on that card: downloads checked and vaulted, blank saves, a kill, a full card
#   the slots             handhelds sharing them: each way decided by content, conflicts held, deletes never sent
# The legacy protocol (unslotted saves, what banana-sync 1.0 writes) is tests/sync.sh's.
HERE=$(cd "$(dirname "$0")/.." && pwd)
OUT=${OUT:-$HERE/tests/out}/sync-slots
rm -rf "$OUT"; mkdir -p "$OUT"
# (each failure also listed in $OUT/failures, one line each however many lines its message has, and counted at the
# end: a run that fails says so with its reasons, a bad from a subshell included)
bad() { echo "FAIL sync-slots: $*"; printf '%s\n' "$(printf '%s' "$*" | tr '\n' ' ')" >> "$OUT/failures"; }
PIDS=""
cleanup() { for p in $PIDS; do kill "$p" 2>/dev/null; done; }
trap cleanup EXIT INT TERM
port() { python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1])'; }
waitport() { for i in $(seq 50); do curl -s "http://127.0.0.1:$1$2" >/dev/null && return 0; sleep 0.1; done; return 1; }

# the library: GBA games enough for the conformance cases (one ROM each, ids from 101) and the checks of the
# stand-in's faults (119, 120), a zipped one, a hack named with its tag, an NDS game
L=$OUT/lib
mkdir -p "$L/gba" "$L/nds"
for i in $(seq 20); do printf 'gba rom %s' "$i" > "$L/gba/Game $i (USA).gba"; done
python3 -c 'import sys, zipfile; z = zipfile.ZipFile(sys.argv[1], "w", zipfile.ZIP_DEFLATED); z.writestr("Zip Game (USA).gba", b"gba rom zipped " * 300); z.writestr("Zip Game (USA) [b].gba", b"second member" * 10)' "$L/gba/Zip Game (USA).zip"
printf 'gba rom hacked' > "$L/gba/Zz Hack (USA) [Hack by Someone v1.0].gba"
head -c 4096 /dev/urandom > "$L/nds/Peel Quest (USA).nds"
P=$(port)
python3 "$HERE/tests/fake-romm.py" "$P" "$L" 2> "$OUT/fake-romm.log" & PIDS="$PIDS $!"
waitport "$P" /api/heartbeat || bad "fake RomM didn't start: $(tail -3 "$OUT/fake-romm.log")"
U=http://127.0.0.1:$P

# ---------------------------------------------------------------- the stand-in is RomM 5.4.1
python3 "$HERE/tests/romm-conformance.py" "$U" > "$OUT/conformance.log" 2>&1 || bad "the stand-in isn't RomM 5.4.1: $(grep FAIL "$OUT/conformance.log")"
if [ -n "$ROMM_URL" ] && [ -n "$ROMM_TOKENS" ]; then
    python3 "$HERE/tests/romm-conformance.py" "$ROMM_URL" "$ROMM_TOKENS" > "$OUT/conformance-real.log" 2>&1 ||
        bad "the real RomM at $ROMM_URL differs: $(grep FAIL "$OUT/conformance-real.log" || tail -1 "$OUT/conformance-real.log")"
else
    echo "SKIP sync-slots: a real RomM (ROMM_URL, ROMM_TOKENS)"
fi

# ---------------------------------------------------------------- the stand-in's faults and tripwires, which the save
# sync's tests lean on: each does what it says
tok() { curl -s -XPOST "$U/test/token" -d '{}' | python3 -c 'import json,sys; print(json.load(sys.stdin)["token"])'; }
jget() { python3 -c "import json,sys; print(json.load(sys.stdin)$1)"; }
T=$(tok); T2=$(tok)
SID=$(curl -s -XPOST "$U/test/save" -d '{"rom_id": 101, "file_name": "Game.srm", "data": "00112233445566778899aabbccddeeff", "slot": "autosave", "emulator": "faults"}' | jget '["id"]')
H="Authorization: Bearer $T"
curl -s -H "$H" "$U/api/saves/$SID/content" -o "$OUT/whole.bin"
[ "$(wc -c < "$OUT/whole.bin")" = 16 ] || bad "a save's content: $(wc -c < "$OUT/whole.bin") bytes"
fault() { curl -s -XPOST "$U/test/fault" -d "{\"path\": \"$1\", \"kind\": \"$2\"${3:+, \"arg\": $3}}" >/dev/null; }
fault "GET /api/saves/[0-9]+/content" truncate
curl -s -H "$H" "$U/api/saves/$SID/content" -o "$OUT/cut.bin"; rc=$?
[ $rc = 18 ] && [ "$(wc -c < "$OUT/cut.bin")" = 8 ] || bad "truncate: curl $rc, $(wc -c < "$OUT/cut.bin") bytes"
fault "GET /api/saves/[0-9]+/content" badhash
curl -s -H "$H" "$U/api/saves/$SID/content" -o "$OUT/bad.bin"
[ "$(wc -c < "$OUT/bad.bin")" = 16 ] && ! cmp -s "$OUT/bad.bin" "$OUT/whole.bin" || bad "badhash: the body wasn't changed"
curl -s -H "$H" "$U/api/saves/$SID/content" -o "$OUT/again.bin"
cmp -s "$OUT/again.bin" "$OUT/whole.bin" || bad "a fault lasted past its count"
fault "GET /api/saves/[0-9]+/content" slow 4
t0=$(date +%s); curl -s -H "$H" "$U/api/saves/$SID/content" -o "$OUT/slow.bin"; t1=$(date +%s)
[ $((t1 - t0)) -ge 3 ] && cmp -s "$OUT/slow.bin" "$OUT/whole.bin" || bad "slow: $((t1 - t0)) s for 16 bytes at 4 a second"
fault "GET /api/heartbeat" skew 86400
d=$(curl -s -D - -o /dev/null "$U/api/heartbeat" | sed -n 's/^Date: //p' | tr -d '\r')
python3 -c "import email.utils,sys,time; sys.exit(0 if abs(email.utils.parsedate_to_datetime(sys.argv[1]).timestamp() - time.time() - 86400) < 5 else 1)" "$d" ||
    bad "skew: Date $d"
up() { printf 'sram %s' "$2" > "$OUT/up.srm"; curl -s -o "$OUT/up.json" -w '%{http_code}' -H "Authorization: Bearer $1" -F "saveFile=@$OUT/up.srm;filename=Game.srm" "$U/api/saves?$3"; }
fault "POST /api/saves" 500
[ "$(up "$T" a 'rom_id=119&slot=autosave&emulator=faults')" = 500 ] || bad "500: not refused"
[ "$(curl -s -H "$H" "$U/api/saves?rom_id=119" | jget '.__len__()')" = 0 ] || bad "500: the refused upload was stored"
fault "GET /api/platforms" raise
[ "$(curl -s -m 5 -o /dev/null -w '%{http_code}' -H "$H" "$U/api/platforms")" = 500 ] || bad "raise: a failing handler left the request unanswered"
grep -q "a fault asked for on GET /api/platforms" "$OUT/fake-romm.log" || bad "raise: no traceback in the stand-in's log"
fault "POST /api/saves" 500-after
[ "$(up "$T" b 'rom_id=119&slot=autosave&emulator=faults')" = 500 ] || bad "500-after: not answered 500"
[ "$(curl -s -H "$H" "$U/api/saves?rom_id=119" | jget '.__len__()')" = 1 ] || bad "500-after: the upload wasn't stored"
curl -s -XPOST "$U/test/strict" -d "{\"tokens\": [\"$T\"]}" >/dev/null
up "$T" c 'rom_id=120&overwrite=true&autocleanup=true' >/dev/null
up "$T2" d 'rom_id=120&overwrite=true' >/dev/null
curl -s -H "$H" -XPUT -F "saveFile=@$OUT/up.srm;filename=Game.srm" "$U/api/saves/$SID" >/dev/null
curl -s -H "$H" -XPOST -H 'Content-Type: application/json' -d "{\"saves\": [$SID]}" "$U/api/saves/delete" >/dev/null
V=$(curl -s "$U/test/violations")
for w in "overwrite=true" "autocleanup" "an upload outside a slot" "a save changed in place" "a save deleted"; do
    echo "$V" | grep -q "$w" || bad "strict: no violation for $w: $V"
done
[ "$(echo "$V" | jget '.__len__()')" = 5 ] || bad "strict: another token's calls were noted too: $V"

# ---------------------------------------------------------------- other RomM clients sharing the slots (tests/romm-peers.py),
# on a stand-in of their own: Grout's GBA saves (NextUI's "<rom>.gba.sav") and a device writing as BananaOS's sync
# will (gpSP's lineage)
L2=$OUT/lib2
mkdir -p "$L2/gba"
printf 'gba rom' > "$L2/gba/Banana Run (USA).gba"          # (id 101)
P2=$(port)
python3 "$HERE/tests/fake-romm.py" "$P2" "$L2" 2> "$OUT/fake-romm2.log" & PIDS="$PIDS $!"
waitport "$P2" /api/heartbeat || bad "the peers' fake RomM didn't start"
U2=http://127.0.0.1:$P2
for n in g1 g2 g3 b; do curl -s -XPOST "$U2/test/token" -d '{}' | jget '["token"]' > "$OUT/$n.token"; done
peer() { n=$1; c=$2; shift 2; python3 "$HERE/tests/romm-peers.py" "$U2" "$(cat "$OUT/$n.token")" "$c" "$OUT/$n" "$@" > "$OUT/$n.json" 2>> "$OUT/peers.err"; }
said() { jget "$2" < "$OUT/$1.json"; }
srv() { curl -s -H "Authorization: Bearer $(cat "$OUT/b.token")" "$U2$1"; }
headof() { srv "/api/saves?rom_id=$1&slot=autosave" | python3 -c 'import json,sys; r=json.load(sys.stdin); print(max(r, key=lambda s: (s["updated_at"], s["id"]))["id"] if r else "")'; }
GS="Saves/GBA/Banana Run (USA).gba.sav"
# a Grout save edited, dated 2 s ahead: Grout compares whole-second mtimes, so an edit in the second of its last sync
# looks unchanged to negotiation until the next second
edit() { head -c 8192 /dev/urandom > "$1"; touch -d "@$(( $(date +%s) + 2 ))" "$1"; }
# (each Grout with the game on its card, as Grout looks for saves only for the ROMs it has)
for n in g1 g2; do mkdir -p "$OUT/$n/Roms/Game Boy Advance (GBA)"; cp "$L2/gba/Banana Run (USA).gba" "$OUT/$n/Roms/Game Boy Advance (GBA)/"; done
mkdir -p "$OUT/g1/Saves/GBA"; head -c 8192 /dev/urandom > "$OUT/g1/$GS"
peer g1 grout sync
[ "$(said g1 '["uploaded"]')" = "['Banana Run (USA).gba.sav']" ] || bad "Grout didn't upload its save: $(cat "$OUT/g1.json")"
srv "/api/saves?rom_id=101" | python3 -c 'import json,re,sys; r=json.load(sys.stdin); sys.exit(0 if len(r) == 1 and r[0]["emulator"] == "GBA" and r[0]["slot"] == "autosave" and re.fullmatch(r"Banana Run \(USA\)\.gba \[[0-9_-]+\]\.sav", r[0]["file_name"]) else 1)' ||
    bad "Grout's version: $(srv "/api/saves?rom_id=101")"
peer g2 grout sync
cmp -s "$OUT/g1/$GS" "$OUT/g2/$GS" || bad "a second Grout didn't get the save: $(cat "$OUT/g2.json")"
peer g3 grout sync
[ -e "$OUT/g3/$GS" ] && bad "a Grout without the game on its card took its save: $(cat "$OUT/g3.json")"
[ "$(srv "/api/sync/sessions/$(said g2 '["session"]')" | jget '["status"]')" = COMPLETED ] || bad "Grout's session wasn't completed"
# both Grouts changed the save: the second to sync is refused (409) and says so, the first's stays the head
edit "$OUT/g1/$GS"; peer g1 grout sync
edit "$OUT/g2/$GS"; peer g2 grout sync
[ "$(said g2 '["conflicts"]')" = "['Banana Run (USA).gba.sav']" ] || bad "the second Grout's change wasn't a conflict: $(cat "$OUT/g2.json")"
srv "/api/saves/$(headof 101)/content" | cmp -s - "$OUT/g1/$GS" || bad "the first Grout's save isn't the head"
# a BananaOS-like device joins and writes its own lineage; Grout takes it, then writes 12 more: its lineage is cut to
# 10 by its autocleanup, BananaOS's version stays, and the head Grout moved refuses BananaOS's next upload
peer b bananaos join 101
printf 'bananaos sram 1' > "$OUT/banana.srm"
sleep 1.1       # (in the same second as Grout's version, negotiation could offer Grout its own: RomM's tie)
peer b bananaos upload 101 "$OUT/banana.srm"; [ "$(cat "$OUT/b.json")" = 200 ] || bad "the BananaOS-like device's upload: $(cat "$OUT/b.json")"
peer g1 grout sync
[ "$(said g1 '["downloaded"]')" = "['Banana Run (USA).gba.sav']" ] || bad "Grout didn't take the newer save: $(cat "$OUT/g1.json")"
for i in $(seq 12); do edit "$OUT/g1/$GS"; peer g1 grout sync; done
lin() { srv "/api/saves?rom_id=101&slot=autosave" | python3 -c "import json,sys; print(sum(1 for s in json.load(sys.stdin) if s['emulator'] == '$1'))"; }
[ "$(lin GBA)" = 10 ] || bad "Grout's lineage after its autocleanup: $(lin GBA) versions, not 10"
[ "$(lin gpSP)" = 1 ] || bad "another lineage was cut by Grout's autocleanup: $(lin gpSP) left"
printf 'bananaos sram 2' > "$OUT/banana.srm"
peer b bananaos upload 101 "$OUT/banana.srm"; [ "$(cat "$OUT/b.json")" = 409 ] || bad "BananaOS's upload after Grout moved the head: HTTP $(cat "$OUT/b.json"), not 409"
[ -s "$OUT/peers.err" ] && bad "a peer failed: $(tail -3 "$OUT/peers.err")"

# ---------------------------------------------------------------- the real card layout (tests/sync-card.sh) the save
# sync's tests use
. "$HERE/tests/sync-card.sh"
RC=$OUT/realcard
realcard "$RC"
for d in saves/gba saves/nds drastic/backup states; do
    [ -d "$RC/$d" ] || bad "realcard: no $d"
done
[ -e "$RC/saves/Snes9x" ] && bad "realcard: a core's save folder, which RetroArch no longer writes"
grep -qx 'core.gba = gpsp' "$RC/banana/settings.conf" && grep -qx 'core.snes = snes9x' "$RC/banana/settings.conf" || bad "realcard: settings: $(cat "$RC/banana/settings.conf")"
[ "$(realsave "$RC" snes "Peel Kart (USA)")" = "$RC/saves/Peel Kart (USA).srm" ] || bad "realsave: SNES path"
[ "$(realsave "$RC" gba "Banana Run (USA)")" = "$RC/saves/gba/Banana Run (USA).srm" ] || bad "realsave: GBA path"
p=$(realsave "$RC" nds "Peel Quest (USA)" 8192)
blank "$OUT/blank.srm" 8192 60
[ "$(wc -c < "$OUT/blank.srm")" = 8192 ] && [ "$(tr -d '\140' < "$OUT/blank.srm" | wc -c)" = 0 ] || bad "blank: not 8192 bytes of 0x60"
# the .dsv cut back to the raw save by nds-saves' own rule
dsv "$p" "$RC/drastic/backup/Peel Quest (USA).dsv"
[ "$(wc -c < "$RC/drastic/backup/Peel Quest (USA).dsv")" = $((8192 + 122)) ] || bad "dsv: not the save and a 122-byte footer"
sed -n '/^raw_of_dsv() {/,/^}/p' "$HERE/../system/rootfs/usr/libexec/banana/nds-saves" > "$OUT/raw_of_dsv.sh"
. "$OUT/raw_of_dsv.sh"
raw_of_dsv "$RC/drastic/backup/Peel Quest (USA).dsv" "$OUT/cut.sav"
cmp -s "$OUT/cut.sav" "$p" || bad "dsv: nds-saves doesn't cut it back to the raw save"
dsv "$p" "$OUT/short.dsv" short; raw_of_dsv "$OUT/short.dsv" "$OUT/cut.sav"
cmp -s "$OUT/cut.sav" "$p" || bad "dsv: nds-saves doesn't cut an older DraStic's 40-byte footer"
for f in version pad; do
    dsv "$p" "$OUT/$f.dsv" $f; rm -f "$OUT/cut.sav"
    raw_of_dsv "$OUT/$f.dsv" "$OUT/cut.sav" && bad "dsv: nds-saves cut a footer whose fields don't add up ($f)"
    [ -e "$OUT/cut.sav" ] && bad "dsv: a footer that doesn't add up ($f) still written out"
done

# ---------------------------------------------------------------- the card's safety layer, on a real card against a
# stand-in of its own: a download is checked against the server's MD5 and only then put in place, the save it replaces
# kept in the vault first; a blank save is never sent and never taken; a run killed or a card full leaves the save as
# it was. The card's token is strict throughout: no overwrite, cleanup, save changed in place or deleted, or upload
# outside a slot
BIN=$OUT/banana-sync
${CXX:-g++} -std=c++17 -O2 -Wall -o "$BIN" "$HERE/src/sync.cpp" "$HERE/src/sync_core.cpp" "$HERE/src/sync_romm.cpp" "$HERE/src/get_list.cpp" \
    "$HERE/src/save_adapt.cpp" "$HERE/src/save_paths.cpp" "$HERE/src/save_vault.cpp" "$HERE/src/json.cpp" -ldl || bad "banana-sync doesn't build"
L3=$OUT/lib3
mkdir -p "$L3/gba"
for g in "Bad Hash" "Blank Card" "Blank Only" "Blanked" "Blank Server" "Full Disk" "Killed" "Truncated" "Unsafe" \
    "Other Bytes" "Two Cores" "Flat" "Sorted Off" "Grout Same" "Grout New" "Padded" "Tail" "Zipped" "Too Big" "Clock" "Resumed" "Starting" "Offline" "Blank Note" "Second Card" "Not Queued" "Not Chosen"; do printf 'gba rom %s' "$g" > "$L3/gba/$g (USA).gba"; done
mkdir -p "$L3/snes" "$L3/nds"; printf 'snes rom' > "$L3/snes/Peel Kart (USA).sfc"; printf 'snes rom blank' > "$L3/snes/Peel Blank (USA).sfc"; printf 'snes rom old' > "$L3/snes/Peel Old (USA).sfc"; printf 'nds rom' > "$L3/nds/Peel Quest (USA).nds"; printf 'nds rom two' > "$L3/nds/Peel Two (USA).nds"
python3 -c 'import sys, zipfile; z = zipfile.ZipFile(sys.argv[1], "w", zipfile.ZIP_DEFLATED); z.writestr("Zip Game (USA).gba", b"gba rom zipped " * 300)' "$L3/gba/Zip Game (USA).zip"
P3=$(port)
python3 "$HERE/tests/fake-romm.py" "$P3" "$L3" 2> "$OUT/fake-romm3.log" & PIDS="$PIDS $!"
waitport "$P3" /api/heartbeat || bad "the card's fake RomM didn't start"
U3=http://127.0.0.1:$P3
T3=$(curl -s -XPOST "$U3/test/token" -d '{}' | jget '["token"]')
curl -s -XPOST "$U3/test/strict" -d "{\"tokens\": [\"$T3\"]}" >/dev/null
C=$OUT/card
realcard "$C"
# (the server's games on the card, but one dumped otherwise; the image's systems.conf; Snes9x installed)
cp "$L3/gba/"* "$C/roms/gba/"; printf 'gba rom another dump' > "$C/roms/gba/Other Bytes (USA).gba"
mkdir -p "$OUT/cores" "$OUT/no-cores"; fakecore "$OUT/cores/snes9x_libretro.so" || bad "the test core can't be put in place"
export BANANA_SYSTEMS="$HERE/../system/rootfs/etc/banana/systems.conf" BANANA_LIBRETRO="$OUT/cores"
printf '{"backend": "romm", "server": "%s", "token": "%s"}\n' "$U3" "$T3" > "$C/banana/romsync.json"
bs() { "$BIN" --root "$C" --run "$C/run" --status "$C/run/sync.status" sync --saves-only > "$OUT/bs.log" 2>&1; }
# (the same, left running: $! is banana-sync itself)
bsbg() { "$BIN" --root "$C" --run "$C/run" --status "$C/run/sync.status" sync --saves-only > "$OUT/bs.log" 2>&1 & }
# bsq <rom>: the sync after a game of it, as the menu runs it
bsq() { "$BIN" --root "$C" --run "$C/run" --status "$C/run/sync.status" sync --auto quit --rom "$1" > "$OUT/bs.log" 2>&1; }
srv3() { curl -s -H "Authorization: Bearer $T3" "$U3$1"; }
rid() { srv3 "/api/roms?platform_ids=$(srv3 /api/platforms | jget '[0]["id"]')&limit=500" | jget "['items']" | python3 -c "import ast,sys; print(next(r['id'] for r in ast.literal_eval(sys.stdin.read()) if r['fs_name'] == '$1 (USA).gba'))"; }
# seed <game> <hex> [<seconds ahead>]: a new version in its autosave slot, as another device left it
seed() { curl -s -XPOST "$U3/test/save" -d "{\"rom_id\": $(rid "$1"), \"slot\": \"autosave\", \"file_name\": \"$1 (USA).srm\", \"data\": \"$2\", \"updated_at\": \"$(date -u -d "@$(( $(date +%s) + ${3:-0} ))" +%Y-%m-%dT%H:%M:%SZ)\"}" | jget '["id"]'; }
hex() { od -An -v -tx1 "$1" | tr -d ' \n'; }
# seedg <game> <file name> <file> [<seconds ahead>]: the same from a file, of any size (its id)
seedg() {
    python3 -c 'import json, sys, time; print(json.dumps({"rom_id": int(sys.argv[1]), "slot": "autosave", "file_name": sys.argv[2], "data": open(sys.argv[3], "rb").read().hex(),
        "updated_at": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(time.time() + int(sys.argv[4])))}))' "$(rid "$1")" "$2" "$3" "${4:-0}" > "$OUT/seed.json"
    curl -s -XPOST "$U3/test/save" -d @"$OUT/seed.json" | jget '["id"]'
}
saves3() { srv3 "/api/saves?rom_id=$(rid "$1")" | jget '.__len__()'; }
vaulted() { ls "$C/banana/save-vault/saves/${3:-gba}/$1 (USA)/" 2>/dev/null | grep -c -- "-$(md5sum < "$2" | cut -c1-8)\.srm$"; }
# gets <id>: how often the save's file has been fetched; heldf <path> <field>: that field of the save held for it
gets() { curl -s "$U3/test/log" | python3 -c 'import json,sys; print(sum(1 for x in json.load(sys.stdin) if x.startswith("GET /api/saves/" + sys.argv[1] + "/content")))' "$1"; }
heldf() { python3 -c 'import json, sys; print(next(v[sys.argv[3]] for v in json.load(open(sys.argv[1]))["held"].values() if v["path"] == sys.argv[2]))' "$C/banana/romsync.json" "$1" "$2"; }
cfault() { curl -s -XPOST "$U3/test/fault" -d "{\"path\": \"GET /api/saves/$1/content\", \"kind\": \"$2\"${3:+, \"arg\": $3}}" >/dev/null; }
# fault3 <path> <kind> [<more fields, as JSON>]: one of the stand-in's faults switched on
fault3() { curl -s -XPOST "$U3/test/fault" -d "{\"path\": \"$1\", \"kind\": \"$2\"${3:+, $3}}" >/dev/null; }
S=$C/saves/gba
# a download cut short or with other bytes than the server's MD5 says: nothing on the card, nothing recorded; then whole
head -c 8192 /dev/urandom > "$OUT/t.srm"
for f in truncate badhash; do
    id=$(seed Truncated "$(hex "$OUT/t.srm")"); cfault "$id" "$f"; bs
    [ -e "$S/Truncated (USA).srm" ] && bad "$f: installed anyway"
    ls "$S" | grep -q '\.part$\|\.new$' && bad "$f: a partial file left in saves/: $(ls "$S")"
    grep -q '"saves/gba/Truncated' "$C/banana/romsync.json" && bad "$f: recorded as synced"
    grep -q 'error: Truncated (USA).srm' "$OUT/bs.log" || bad "$f: not reported: $(cat "$OUT/bs.log")"
done
bs; cmp -s "$S/Truncated (USA).srm" "$OUT/t.srm" || bad "a whole download wasn't installed: $(cat "$OUT/bs.log")"
# a fresh core's blank save on a card that never synced: never sent, the server's taken over it, the blank vaulted;
# a blank with nothing on the server stays here only
head -c 8192 /dev/urandom > "$OUT/real.srm"; seed "Blank Card" "$(hex "$OUT/real.srm")" >/dev/null
blank "$S/Blank Card (USA).srm" 131072 ff; cp "$S/Blank Card (USA).srm" "$OUT/blank-ff.srm"
blank "$S/Blank Only (USA).srm" 131072 ff
bs
cmp -s "$S/Blank Card (USA).srm" "$OUT/real.srm" || bad "the server's save didn't replace a blank one: $(cat "$OUT/bs.log")"
[ "$(vaulted "Blank Card" "$OUT/blank-ff.srm")" = 1 ] || bad "the blank save it replaced isn't in the vault"
[ "$(saves3 "Blank Card")" = 1 ] && [ "$(saves3 "Blank Only")" = 0 ] || bad "a blank save was sent: $(saves3 "Blank Card"), $(saves3 "Blank Only")"
# a blank on the server is never taken
seed "Blank Server" "$(printf '%08192d' 0 | tr 0 '\000' | od -An -v -tx1 | tr -d ' \n')" >/dev/null; bs
[ -e "$S/Blank Server (USA).srm" ] && bad "a blank server save was installed"
grep -q 'error: Blank Server' "$OUT/bs.log" && bad "a blank server save is an error: $(cat "$OUT/bs.log")"
grep -q 'blank on the server, not taken: Blank Server' "$OUT/bs.log" || bad "a blank server save not said: $(cat "$OUT/bs.log")"
# a save that synced and is blank since (the emulator's data cleared, perhaps on purpose): held, neither sent nor
# refilled from the server
head -c 8192 /dev/urandom > "$S/Blanked (USA).srm"; bs
blank "$S/Blanked (USA).srm" 131072 ff; cp "$S/Blanked (USA).srm" "$OUT/blanked.srm"; bs
cmp -s "$S/Blanked (USA).srm" "$OUT/blanked.srm" || bad "a save blanked since the last sync was refilled: $(cat "$OUT/bs.log")"
[ "$(saves3 Blanked)" = 1 ] || bad "a save blanked since the last sync was sent: $(saves3 Blanked)"
grep -q 'blank here since the last sync, left as it is: saves/gba/Blanked' "$OUT/bs.log" || bad "the held blank not said: $(cat "$OUT/bs.log")"
grep -q 'error:' "$OUT/bs.log" && bad "a held blank is an error: $(cat "$OUT/bs.log")"
# (counted with the conflicts for the menu's wait: a game isn't played from nothing over the server's save unasked)
[ "$(awk '{print $6}' "$C/run/sync.status")" -ge 1 ] || bad "a held blank, the server's copy there to take back, not counted for the menu: $(cat "$C/run/sync.status")"
# a new save written where the blanked one was (the game started over): not the agreed save played on, so never sent
# over the server's as the newer: a conflict, both kept
head -c 8192 /dev/urandom > "$S/Blanked (USA).srm"; cp "$S/Blanked (USA).srm" "$OUT/blanked2.srm"; bs
grep -q 'conflict, both kept: saves/gba/Blanked' "$OUT/bs.log" && [ "$(saves3 Blanked)" = 1 ] && cmp -s "$S/Blanked (USA).srm" "$OUT/blanked2.srm" ||
    bad "a new save where a blanked one was, sent over the server's: $(saves3 Blanked) versions: $(cat "$OUT/bs.log")"
# and it stays a conflict: the record of the lost version is still there at the next syncs, and must not read as the
# save played on from it
for i in 1 2; do bs
    grep -q 'conflict, both kept: saves/gba/Blanked' "$OUT/bs.log" && [ "$(saves3 Blanked)" = 1 ] && cmp -s "$S/Blanked (USA).srm" "$OUT/blanked2.srm" ||
        bad "a conflict over a lost save fell away at sync $i, the new save sent: $(saves3 Blanked) versions: $(cat "$OUT/bs.log")"
done
# its owner keeping the new one agrees it, and the mark goes with that: the next change is sent as any other
BK=$("$BIN" --root "$C" --run "$C/run" saves | awk -F'\t' '$3 == "saves/gba/Blanked (USA).srm" { print $2; exit }')
[ "$("$BIN" --root "$C" --run "$C/run" resolve "$BK" card | tail -1)" = ok ] && [ "$(saves3 Blanked)" = 2 ] || bad "a lost save's new one not kept: $(saves3 Blanked) versions"
head -c 8192 /dev/urandom > "$S/Blanked (USA).srm"; bs
grep -q 'up saves/gba/Blanked' "$OUT/bs.log" && [ "$(saves3 Blanked)" = 3 ] && ! grep -q 'conflict, both kept: saves/gba/Blanked' "$OUT/bs.log" ||
    bad "a lost save's mark kept past an agreement: $(saves3 Blanked) versions: $(cat "$OUT/bs.log")"
# gone from the card, not blank, then a new save there: the same, over syncs
rm "$S/Blanked (USA).srm"; bs; head -c 8192 /dev/urandom > "$S/Blanked (USA).srm"; cp "$S/Blanked (USA).srm" "$OUT/blanked3.srm"
for i in 1 2 3; do bs
    grep -q 'conflict, both kept: saves/gba/Blanked' "$OUT/bs.log" && [ "$(saves3 Blanked)" = 3 ] && cmp -s "$S/Blanked (USA).srm" "$OUT/blanked3.srm" ||
        bad "a new save where a deleted one was, sync $i: $(saves3 Blanked) versions: $(cat "$OUT/bs.log")"
done
# a save blanked since it synced, put back from the Saves page: the server's copy is of the game's size, the blank of
# the fresh core's, and the blank is no measure of it
head -c 32768 /dev/urandom > "$S/Blank Note (USA).srm"; cp "$S/Blank Note (USA).srm" "$OUT/note.srm"; bs
blank "$S/Blank Note (USA).srm" 8192 ff; bs
NK=$("$BIN" --root "$C" --run "$C/run" saves | awk -F'\t' '$3 == "saves/gba/Blank Note (USA).srm" { print $2; exit }')
[ -n "$NK" ] && [ "$("$BIN" --root "$C" --run "$C/run" resolve "$NK" server | tail -1)" = ok ] && cmp -s "$S/Blank Note (USA).srm" "$OUT/note.srm" ||
    bad "a blanked save not put back from the server's copy of another size: $NK: $(cat "$OUT/bs.log")"
# an install a run cut short left open in the journal: checked at the start of the next sync, said, and closed
printf 'begin\trec.1\tinstall\tsaves/gba/Blank Card (USA).srm\t%s\t\t\t\t%s\t20260101T000000Z\n' "$(md5sum < "$S/Blank Card (USA).srm" | cut -c1-32)" \
    "$(md5sum < "$OUT/blank-ff.srm" | cut -c1-32)" >> "$C/banana/logs/sync/journal.log"; bs
grep -q 'journal: unchanged saves/gba/Blank Card (USA).srm' "$OUT/bs.log" || bad "an open operation not recovered at the start: $(cat "$OUT/bs.log")"
grep -q '^end	rec\.1	recovered unchanged	' "$C/banana/logs/sync/journal.log" || bad "the recovered operation not closed in the journal"
# killed in the middle of a download: the save as it was, no operation left open; the next run takes it, the old one
# kept, so every version is still somewhere
head -c 8192 /dev/urandom > "$S/Killed (USA).srm"; cp "$S/Killed (USA).srm" "$OUT/k1.srm"; bs
[ "$(saves3 Killed)" = 1 ] || bad "the first version wasn't sent: $(cat "$OUT/bs.log")"
cmp -s "$C/banana/save-vault/saves/gba/Killed (USA)/last-good.srm" "$OUT/k1.srm" || bad "last-good isn't the save just sent"
head -c 8192 /dev/urandom > "$OUT/k2.srm"; id=$(seed Killed "$(hex "$OUT/k2.srm")" 5); cfault "$id" slow 512
bsbg; k=$!; sleep 1.5; kill -9 $k 2>/dev/null; wait $k 2>/dev/null
cmp -s "$S/Killed (USA).srm" "$OUT/k1.srm" || bad "a killed download changed the save"
awk -F'\t' '$1 == "begin" { o[$2] = 1 } $1 == "end" { delete o[$2] } END { for (i in o) exit 1 }' "$C/banana/logs/sync/journal.log" 2>/dev/null ||
    bad "an operation left open by a kill during the download: $(cat "$C/banana/logs/sync/journal.log")"
bs; cmp -s "$S/Killed (USA).srm" "$OUT/k2.srm" || bad "the download after a kill: $(cat "$OUT/bs.log")"
[ "$(vaulted Killed "$OUT/k1.srm")" = 1 ] || bad "the save a download replaced isn't in the vault"
cmp -s "$C/banana/save-vault/saves/gba/Killed (USA)/last-good.srm" "$OUT/k2.srm" || bad "last-good isn't the newest save"
grep -q "	install	saves/gba/Killed (USA).srm	$(md5sum < "$OUT/k1.srm" | cut -c1-32)	" "$C/banana/logs/sync/journal.log" || bad "the install isn't in the journal"
# a download that breaks off: nothing of it left in /run, which is memory (the next run has another name for it)
rm -f "$C/run"/sync-dl.*; head -c 8192 /dev/urandom > "$OUT/k3.srm"; id=$(seed Killed "$(hex "$OUT/k3.srm")" 7); cfault "$id" truncate; bs
ls "$C/run" | grep -q '^sync-dl' && bad "a broken download left: $(ls "$C/run" | tr '\n' ' ')"
bs; cmp -s "$S/Killed (USA).srm" "$OUT/k3.srm" || bad "the download after one that broke: $(cat "$OUT/bs.log")"
# refused (409) though the head is the agreed version (RomM without this device's record of it, which its listing
# said it had): marked seen and sent once more, not held as a conflict
head -c 8192 /dev/urandom > "$S/Killed (USA).srm"
fault3 'POST /api/saves$' 409; n=$(saves3 Killed); bs
grep -q 'refused over the agreed version, marked seen and sent again: saves/gba/Killed' "$OUT/bs.log" && [ "$(saves3 Killed)" = $((n + 1)) ] && ! grep -q 'conflict, both kept: saves/gba/Killed' "$OUT/bs.log" ||
    bad "a 409 over the agreed head: $(saves3 Killed) versions, was $n: $(cat "$OUT/bs.log")"
# a save from the server replaces the game's quick-resume states too: RetroArch's automatic slot in each GBA core's
# folder holds the old save's memory, and loaded over the new save it would be written back over it at the game's end;
# another system's state of the same name stays
mkdir -p "$C/states/gpSP" "$C/states/mGBA" "$C/states/Snes9x"
for d in gpSP mGBA Snes9x; do echo spot > "$C/states/$d/Resumed (USA).state.auto"; done
head -c 8192 /dev/urandom > "$S/Resumed (USA).srm"; bs
head -c 8192 /dev/urandom > "$OUT/r2.srm"; seed Resumed "$(hex "$OUT/r2.srm")" 5 >/dev/null; bs
cmp -s "$S/Resumed (USA).srm" "$OUT/r2.srm" || bad "a download with resume states beside it: $(cat "$OUT/bs.log")"
[ ! -e "$C/states/gpSP/Resumed (USA).state.auto" ] && [ ! -e "$C/states/mGBA/Resumed (USA).state.auto" ] || bad "RetroArch's automatic state kept beside a save from the server"
[ -e "$C/states/Snes9x/Resumed (USA).state.auto" ] || bad "another system's state of the same name removed with a GBA save"
# refused twice over the agreed head: held as any other refusal, nothing sent past it
head -c 8192 /dev/urandom > "$S/Killed (USA).srm"; n=$(saves3 Killed)
fault3 'POST /api/saves$' 409 '"count": 2'; bs
[ "$(saves3 Killed)" = "$n" ] && grep -q 'marked seen and sent again: saves/gba/Killed' "$OUT/bs.log" && [ "$(grep -c 'up saves/gba/Killed' "$OUT/bs.log")" = 0 ] || bad "refused twice: $(saves3 Killed) versions, was $n: $(cat "$OUT/bs.log")"
bs
# refused, the head's own row written over meanwhile: the same id, other bytes, so not the agreed version: never marked
# seen and sent over
head -c 8192 /dev/urandom > "$S/Resumed (USA).srm"; cp "$S/Resumed (USA).srm" "$OUT/rw.srm"
head -c 8192 /dev/urandom > "$OUT/rw-srv.srm"; fault3 'POST /api/saves$' 409-rewrite "\"arg\": \"$(hex "$OUT/rw-srv.srm")\""; n=$(saves3 Resumed); bs
grep -q 'marked seen and sent again: saves/gba/Resumed' "$OUT/bs.log" && bad "a head of other bytes under the agreed id taken for the agreed version: $(cat "$OUT/bs.log")"
grep -q 'conflict, both kept: saves/gba/Resumed' "$OUT/bs.log" && [ "$(saves3 Resumed)" = "$n" ] || bad "a head rewritten in place: not held: $(saves3 Resumed), was $n: $(cat "$OUT/bs.log")"
# the same, the head then giving no MD5 at all: its id alone says nothing of its bytes, so it isn't the agreed version
head -c 8192 /dev/urandom > "$S/Killed (USA).srm"; head -c 8192 /dev/urandom > "$OUT/rw2-srv.srm"
fault3 'POST /api/saves$' 409-rewrite-nohash "\"arg\": \"$(hex "$OUT/rw2-srv.srm")\""; n=$(saves3 Killed); bs
! grep -q 'marked seen and sent again: saves/gba/Killed' "$OUT/bs.log" && [ "$(saves3 Killed)" = "$n" ] || bad "a head with no MD5 under the agreed id taken for the agreed version: $(cat "$OUT/bs.log")"
seed Killed "$(hex "$S/Killed (USA).srm")" 9 >/dev/null; bs
# a full card (writes past 16 KiB fail, as on a card with no room: EFBIG for ENOSPC): the vault copy of gpSP's 128 KiB
# (8 KiB of save, padded with 0xFF) can't be made, so it isn't replaced; a download too big to write leaves it too
full() { python3 -c 'import os, resource, signal, sys; signal.signal(signal.SIGXFSZ, signal.SIG_IGN); resource.setrlimit(resource.RLIMIT_FSIZE, (16384, 16384)); os.execv(sys.argv[1], sys.argv[1:])' \
    "$BIN" --root "$C" --run "$C/run" --status "$C/run/sync.status" sync --saves-only > "$OUT/bs.log" 2>&1; }
blank "$OUT/pad.bin" 122880 ff; head -c 8192 /dev/urandom | cat - "$OUT/pad.bin" > "$S/Full Disk (USA).srm"; cp "$S/Full Disk (USA).srm" "$OUT/f1.srm"; bs
head -c 8192 /dev/urandom > "$OUT/f2.srm"; seed "Full Disk" "$(hex "$OUT/f2.srm")" 10 >/dev/null; full
cmp -s "$S/Full Disk (USA).srm" "$OUT/f1.srm" || bad "a full card: the save was replaced without its vault copy"
grep -q 'Full Disk.*vault' "$OUT/bs.log" || bad "a full card: the vault's failure not reported: $(cat "$OUT/bs.log")"
head -c 32768 /dev/urandom > "$OUT/f3.srm"; seed "Full Disk" "$(hex "$OUT/f3.srm")" 20 >/dev/null; full
cmp -s "$S/Full Disk (USA).srm" "$OUT/f1.srm" || bad "a full card: a download too big to write changed the save"
grep -q 'error: Full Disk (USA).srm' "$OUT/bs.log" || bad "a full card: a download too big to write not reported: $(cat "$OUT/bs.log")"
ls "$S" | grep -q '\.part$\|\.new$' && bad "a full card: a partial file left in saves/"
# a game about to start (the menu's game.starting, before its game.pid can be written): nothing placed, decided next time
head -c 8192 /dev/urandom > "$S/Starting (USA).srm"; bs; head -c 8192 /dev/urandom > "$OUT/st2.srm"; seed Starting "$(hex "$OUT/st2.srm")" 5 >/dev/null
sleep 30 & sp=$!; echo $sp > "$C/run/game.starting"; bs; kill $sp 2>/dev/null; rm -f "$C/run/game.starting"
cmp -s "$S/Starting (USA).srm" "$OUT/st2.srm" && bad "a save placed under a game about to start"
grep -q 'a game is running, next time' "$OUT/bs.log" || bad "a game about to start not seen: $(cat "$OUT/bs.log")"
bs; cmp -s "$S/Starting (USA).srm" "$OUT/st2.srm" || bad "the download after a game about to start: $(cat "$OUT/bs.log")"
# the game started again after Power off after sleeping, before it starts (resume-once names it): a sync of the saves
# leaves it as it is, its resume state belonging with the save here; started and ended, its own sync takes the server's
head -c 8192 /dev/urandom > "$OUT/rs2.srm"; seed Starting "$(hex "$OUT/rs2.srm")" 10 >/dev/null
printf '%s\n' "$C/roms/gba/Starting (USA).gba" > "$C/run/resume-once"; bs
cmp -s "$S/Starting (USA).srm" "$OUT/st2.srm" && grep -q 'not synced, about to start again after Power off after sleeping.*Starting' "$OUT/bs.log" || bad "the game being resumed synced underneath: $(cat "$OUT/bs.log")"
rm "$C/run/resume-once"; bs; cmp -s "$S/Starting (USA).srm" "$OUT/rs2.srm" || bad "the game resumed, synced after: $(cat "$OUT/bs.log")"
# the state file's rename cut by the power: the new copy whole beside it, nothing under its name; read from the copy,
# and written back whole by the next run
mv "$C/banana/romsync.json" "$C/banana/romsync.json.new"
"$BIN" --root "$C" --run "$C/run" status | grep -q "paired with" || bad "the state file's copy beside it not read: $("$BIN" --root "$C" --run "$C/run" status)"
bs; [ -s "$C/banana/romsync.json" ] && [ ! -e "$C/banana/romsync.json.new" ] && grep -q '"slots"' "$C/banana/romsync.json" || bad "the state file not written back from its copy: $(ls "$C/banana")"
# the pairing forgotten while a sync runs: not written back by it when it ends
cp "$C/banana/romsync.json" "$OUT/conf.saved"
fault3 'GET /api/saves$' delay '"arg": 2'
bsbg; k=$!; sleep 0.7; "$BIN" --root "$C" --run "$C/run" forget; wait $k
[ -e "$C/banana/romsync.json" ] && bad "a sync wrote the pairing back after forget"
cp "$OUT/conf.saved" "$C/banana/romsync.json"
# the sync after a game with the server out of reach: the save kept in the vault at once and left waiting, so the sync
# when Wi-Fi comes back sends it: under its slot when it synced before, under its ROM when it never did
head -c 8192 /dev/urandom > "$S/Killed (USA).srm"; cp "$S/Killed (USA).srm" "$OUT/k5.srm"; head -c 8192 /dev/urandom > "$S/Offline (USA).srm"
sed "s#\"server\": \"$U3\"#\"server\": \"http://127.0.0.1:1\"#" "$OUT/conf.saved" > "$C/banana/romsync.json"
printf 'homesync_auto = quit\n' >> "$C/banana/settings.conf"
bsq "gba/Killed (USA).gba"
grep -q 'waiting to be sent, the server out of reach (a copy in the vault): saves/gba/Killed' "$OUT/bs.log" && [ "$(vaulted Killed "$OUT/k5.srm")" = 1 ] &&
    python3 -c 'import json, sys; q = json.load(open(sys.argv[1]))["queue"]; sys.exit(0 if any(v["md5"] == sys.argv[2] and v["vault"] for v in q.values()) else 1)' "$C/banana/romsync.json" "$(md5sum < "$OUT/k5.srm" | cut -c1-32)" ||
    bad "the sync after a game, the server out of reach: $(cat "$OUT/bs.log")"
grep -q '|.*; kept to send with the next sync$' "$C/run/gameend.status" || bad "the sync after a game, the server out of reach, not said: $(cat "$C/run/gameend.status")"
bsq "gba/Offline (USA).gba"
grep -q 'waiting to be sent, the server out of reach (a copy in the vault): saves/gba/Offline' "$OUT/bs.log" && [ "$(vaulted Offline "$S/Offline (USA).srm")" = 1 ] &&
    [ "$(python3 -c 'import json, sys; print(json.load(open(sys.argv[1]))["queue"]["rom:gba/Offline (USA).gba"]["path"])' "$C/banana/romsync.json")" = "saves/gba/Offline (USA).srm" ] ||
    bad "a game never synced, the server out of reach: $(cat "$OUT/bs.log")"
"$BIN" --root "$C" --run "$C/run" saves | grep -q '^queued	rom:gba/Offline (USA).gba	saves/gba/Offline (USA).srm	' || bad "a game never synced, waiting: not listed: $("$BIN" --root "$C" --run "$C/run" saves | grep queued)"
# a system whose ROM folder isn't named as it is (systems.conf's folder =): its save found and kept all the same
sed 's/^\[snes\]$/[snes]\nfolder = sfc/' "$BANANA_SYSTEMS" > "$OUT/sys-remap.conf"; mkdir -p "$C/roms/sfc"; cp "$L3/snes/Peel Kart (USA).sfc" "$C/roms/sfc/"
realsave "$C" snes "Peel Kart (USA)" >/dev/null; cp "$C/saves/Peel Kart (USA).srm" "$OUT/remap.srm"
BANANA_SYSTEMS=$OUT/sys-remap.conf bsq "sfc/Peel Kart (USA).sfc"
[ "$(ls "$C/banana/save-vault/saves/Peel Kart (USA)/" 2>/dev/null | grep -c -- "-$(md5sum < "$OUT/remap.srm" | cut -c1-8)\.srm$")" = 1 ] && grep -q 'waiting to be sent.*saves/Peel Kart' "$OUT/bs.log" ||
    bad "a system in a folder of another name, its save not kept out of reach: $(cat "$OUT/bs.log")"
rm -r "$C/roms/sfc" "$C/saves/Peel Kart (USA).srm"
python3 -c 'import json, sys; c = json.load(open(sys.argv[1])); c["queue"].pop("rom:sfc/Peel Kart (USA).sfc"); json.dump(c, open(sys.argv[1], "w"))' "$C/banana/romsync.json"
# a game of the card's own, which the server doesn't list, played with the server out of reach: its save waits under its
# ROM like any never synced, and stops waiting at the first sync that has the server's list (below, when Wi-Fi is back)
printf 'a game of the card own' > "$C/roms/gba/Own Game (USA).gba"; head -c 8192 /dev/urandom > "$S/Own Game (USA).srm"; cp "$S/Own Game (USA).srm" "$OUT/own.srm"
bsq "gba/Own Game (USA).gba"
"$BIN" --root "$C" --run "$C/run" saves | grep -q '^queued	rom:gba/Own Game (USA).gba	' || bad "a game of the card's own, the server out of reach: not waiting: $(cat "$OUT/bs.log")"
# Romhost out of reach, the same: kept in the vault
sed 's#"backend": "romm"#"backend": "romhost"#' "$C/banana/romsync.json" > "$OUT/rh.json"; cp "$C/banana/romsync.json" "$OUT/rm.json"; cp "$OUT/rh.json" "$C/banana/romsync.json"
head -c 8192 /dev/urandom > "$S/Offline (USA).srm"
bsq "gba/Offline (USA).gba"
grep -q 'kept in the vault, the server out of reach: saves/gba/Offline' "$OUT/bs.log" && [ "$(vaulted Offline "$S/Offline (USA).srm")" = 1 ] || bad "Romhost out of reach, the save not kept: $(cat "$OUT/bs.log")"
# Romhost answering: a stand-in serving the plan the test writes
RHPORT=$(port); echo '{"actions": []}' > "$OUT/rh-plan.json"
python3 "$HERE/tests/fake-romhost.py" "$RHPORT" "$OUT/rh-plan.json" 2> "$OUT/fake-romhost.log" & PIDS="$PIDS $!"
waitport "$RHPORT" /api/v1/saves/rules || bad "fake-romhost.py didn't start"
sed "s#http://127.0.0.1:1\"#http://127.0.0.1:$RHPORT\"#" "$OUT/rh.json" > "$C/banana/romsync.json"
# a game about to start again after Power off after sleeping: Romhost's download of its save waits too, and of its
# states, RetroArch's automatic one among them (the resume state itself)
mkdir -p "$C/states/mGBA"; echo spot > "$C/states/mGBA/Killed (USA).state.auto"; cp "$S/Killed (USA).srm" "$OUT/k-rh.srm"
echo '{"mode": "states", "actions": [{"path": "saves/gba/Killed (USA).srm", "action": "download"}, {"path": "states/mGBA/Killed (USA).state.auto", "action": "download"}]}' > "$OUT/rh-plan.json"
echo "$C/roms/gba/Killed (USA).gba" > "$C/run/resume-once"
bs
grep -q 'not synced, about to start again after Power off after sleeping, its resume state with the save here: saves/gba/Killed' "$OUT/bs.log" && cmp -s "$S/Killed (USA).srm" "$OUT/k-rh.srm" || bad "Romhost placed a save under a game about to resume: $(cat "$OUT/bs.log")"
[ "$(cat "$C/states/mGBA/Killed (USA).state.auto")" = spot ] || bad "Romhost placed a state under a game about to resume: $(cat "$OUT/bs.log")"
rm -f "$C/run/resume-once" "$C/states/mGBA/Killed (USA).state.auto"
# an upload refused after the plan (Wi-Fi gone mid-run): what was played is kept in the vault, as on RomM
head -c 8192 /dev/urandom > "$S/Offline (USA).srm"
echo '{"actions": [{"path": "saves/gba/Offline (USA).srm", "action": "upload", "expect": 0}], "put": 500}' > "$OUT/rh-plan.json"; bs
grep -q 'not sent, kept in the vault: saves/gba/Offline' "$OUT/bs.log" && [ "$(vaulted Offline "$S/Offline (USA).srm")" = 1 ] || bad "Romhost's refused upload not kept: $(cat "$OUT/bs.log")"
# a save written during a slow fetch (a game played and quit meanwhile): the server's version doesn't go over it, and
# isn't recorded as agreed
echo '{"actions": [{"path": "saves/gba/Offline (USA).srm", "action": "download"}], "delay": 2}' > "$OUT/rh-plan.json"; bsbg; RHP=$!
sleep 1; head -c 8192 /dev/urandom > "$S/Offline (USA).srm"; cp "$S/Offline (USA).srm" "$OUT/rh-played.srm"; wait $RHP
cmp -s "$S/Offline (USA).srm" "$OUT/rh-played.srm" && grep -q 'Offline (USA).srm: changed while syncing' "$OUT/bs.log" ||
    bad "Romhost's download went over a save written during it: $(cat "$OUT/bs.log")"
grep -q '"saves/gba/Offline (USA).srm": {"version": 2' "$C/banana/romsync.json" && bad "Romhost's download over a save written during it recorded as agreed"
cp "$OUT/rm.json" "$C/banana/romsync.json"
sed -i "s#\"server\": \"http://127.0.0.1:1\"#\"server\": \"$U3\"#" "$C/banana/romsync.json"
head -c 8192 /dev/urandom > "$S/Not Queued (USA).srm"
n=$(saves3 Killed); "$BIN" --root "$C" --run "$C/run" --status "$C/run/sync.status" sync --auto wifi > "$OUT/bs.log" 2>&1
[ "$(saves3 Killed)" = $((n + 1)) ] && ! grep -q '"queue": {$' "$C/banana/romsync.json" || bad "the save left waiting not sent when Wi-Fi came back: $(cat "$OUT/bs.log")"
[ "$(saves3 Offline)" = 1 ] || bad "a game never synced, left waiting, not sent when Wi-Fi came back: $(cat "$OUT/bs.log")"
[ "$(saves3 "Not Queued")" = 0 ] || bad "the Wi-Fi sync sent more than the queue: $(cat "$OUT/bs.log")"
# (the game of the card's own: no longer waiting, said, its copy still in the vault; and nothing left to bring the next
# return of Wi-Fi to list the server's games again)
grep -q 'no longer waiting to be sent, not a game the server lists.*gba/Own Game (USA).gba' "$OUT/bs.log" && [ "$(vaulted "Own Game" "$OUT/own.srm")" = 1 ] &&
    ! "$BIN" --root "$C" --run "$C/run" saves | grep -q '^queued	rom:' || bad "a game the server doesn't list still waits to be sent: $("$BIN" --root "$C" --run "$C/run" saves | grep queued) $(cat "$OUT/bs.log")"
n=$(curl -s "$U3/test/log" | jget '.__len__()')	# (each look at the log is in it: the next one is one more)
"$BIN" --root "$C" --run "$C/run" --status "$C/run/sync.status" sync --auto wifi > "$OUT/bs-wifi2.log" 2>&1
[ "$(curl -s "$U3/test/log" | jget '.__len__()')" = $((n + 1)) ] || bad "Wi-Fi back with nothing waiting: the server asked again: $(cat "$OUT/bs-wifi2.log")"
rm -f "$C/roms/gba/Own Game (USA).gba" "$S/Own Game (USA).srm"
rm "$S/Not Queued (USA).srm"
# the server answering but not its save listing: the same, kept and waiting
head -c 8192 /dev/urandom > "$S/Killed (USA).srm"; cp "$S/Killed (USA).srm" "$OUT/k6.srm"
fault3 'POST /api/sync/negotiate' 500
bsq "gba/Killed (USA).gba"
grep -q 'waiting to be sent, the server out of reach (a copy in the vault): saves/gba/Killed' "$OUT/bs.log" && [ "$(vaulted Killed "$OUT/k6.srm")" = 1 ] || bad "the save listing failing after a game: $(cat "$OUT/bs.log")"
# gone from the card while it waits: the server's copy still fetched, so the menu can ask before a game plays from nothing
rm "$S/Killed (USA).srm"; bs; "$BIN" --root "$C" --run "$C/run" saves | grep -q '^held	[0-9]*/autosave	saves/gba/Killed (USA).srm	missing here since the last sync	.*	server	' &&
    [ "$(awk '{print $6}' "$C/run/sync.status")" -ge 1 ] || bad "a waiting save gone from the card: nothing to choose: $("$BIN" --root "$C" --run "$C/run" saves | grep Killed) $(cat "$C/run/sync.status")"
# the server's taken back: the save that waited never reached the server, so its copy is the only one, and kept
KK=$("$BIN" --root "$C" --run "$C/run" saves | awk -F'\t' '$3 == "saves/gba/Killed (USA).srm" { print $2; exit }')
QV=$(python3 -c 'import json, sys; print(json.load(open(sys.argv[1]))["queue"][sys.argv[2]]["vault"])' "$C/banana/romsync.json" "$KK")
[ "$("$BIN" --root "$C" --run "$C/run" resolve "$KK" server | tail -1)" = ok ] || bad "a waiting save gone: the server's not taken"
bs; grep -qF "never sent, kept in the vault: $QV" "$OUT/bs.log" && [ -e "$QV" ] ||
    bad "a save that waited, the server's taken over it: its copy not kept: $(cat "$OUT/bs.log")"
cp "$OUT/k6.srm" "$S/Killed (USA).srm"; bs
sed -i '/^homesync_auto = quit/d' "$C/banana/settings.conf"
# a server name that climbs out of saves/: nothing written anywhere
curl -s -XPOST "$U3/test/save" -d "{\"rom_id\": $(rid Unsafe), \"slot\": \"autosave\", \"file_name\": \"../../x.srm\", \"data\": \"00ff00ff\"}" >/dev/null; bs
[ -z "$(find "$OUT" -name 'x.srm')" ] || bad "a server name wrote outside saves/: $(find "$OUT" -name 'x.srm')"

# ---------------------------------------------------------------- each game's save where its emulator reads it: the
# DS's saves/nds, every GBA core's saves/gba, another system's its core's folder (the core asked its name); a game is
# the server's only by its ROM's contents
ridf() { for p in $(srv3 /api/platforms | python3 -c 'import json,sys; print(" ".join(str(p["id"]) for p in json.load(sys.stdin)))'); do
    srv3 "/api/roms?platform_ids=$p&limit=500" | python3 -c "import json,sys; [print(r['id']) for r in json.load(sys.stdin)['items'] if r['fs_name'] == sys.argv[1]]" "$1"; done; }
nsaves() { srv3 "/api/saves?rom_id=$(ridf "$1")" | jget '.__len__()'; }
seedf() { curl -s -XPOST "$U3/test/save" -d "{\"rom_id\": $(ridf "$1"), \"slot\": \"autosave\", \"file_name\": \"$2\", \"data\": \"$3\", \"updated_at\": \"$(date -u -d "@$(( $(date +%s) + ${4:-0} ))" +%Y-%m-%dT%H:%M:%SZ)\"}" >/dev/null; }
# SNES: up from the flat saves/ RetroArch and BananaFront keep, a newer one from the server down into it; nothing in
# Snes9x's folder, which no emulator reads any more
K=$C/saves/"Peel Kart (USA).srm"; realsave "$C" snes "Peel Kart (USA)" >/dev/null; bs
[ "$(nsaves "Peel Kart (USA).sfc")" = 1 ] || bad "a SNES save in the flat saves/ wasn't sent: $(cat "$OUT/bs.log")"
head -c 8192 /dev/urandom > "$OUT/kart.srm"; seedf "Peel Kart (USA).sfc" "Peel Kart (USA).srm" "$(hex "$OUT/kart.srm")" 5; bs
cmp -s "$K" "$OUT/kart.srm" || bad "a SNES save from the server didn't go into the flat saves/: $(cat "$OUT/bs.log")"
[ -e "$C/saves/Snes9x/Peel Kart (USA).srm" ] && bad "a SNES save went into Snes9x's folder"
# a save still in a core's folder from before 0.5.3 (no game started since the update, so saves-flat hasn't run this
# boot): the sync runs saves-flat first (the real one), and the save the next game reads is the one sent
cp "$L3/snes/Peel Old (USA).sfc" "$C/roms/snes/"; mkdir -p "$C/saves/Snes9x"; head -c 8192 /dev/urandom > "$C/saves/Snes9x/Peel Old (USA).srm"
cp "$C/saves/Snes9x/Peel Old (USA).srm" "$OUT/old.srm"; rm -f "$C/run/saves-flat"
BANANA_SAVES_FLAT=$HERE/../system/rootfs/usr/libexec/banana/saves-flat bs
cmp -s "$C/saves/Peel Old (USA).srm" "$OUT/old.srm" && [ ! -e "$C/saves/Snes9x/Peel Old (USA).srm" ] && [ -f "$C/run/saves-flat" ] ||
    bad "a core folder's save not moved flat before the sync: $(find "$C/saves" -name 'Peel Old*')"
[ "$(nsaves "Peel Old (USA).sfc")" = 1 ] || bad "a save moved flat by saves-flat wasn't sent: $(cat "$OUT/bs.log")"
# a fresh core's blank save, of its own size, against a real one of another on the server: the blank says nothing of
# the game's size, so the server's replaces it (kept in the vault), not held as a format this handheld can't use
cp "$L3/snes/Peel Blank (USA).sfc" "$C/roms/snes/"; PB=$C/saves/"Peel Blank (USA).srm"
blank "$PB" 8192 60; cp "$PB" "$OUT/pb-blank.srm"; head -c 2048 /dev/urandom > "$OUT/pb.srm"
seedf "Peel Blank (USA).sfc" "Peel Blank (USA).srm" "$(hex "$OUT/pb.srm")" 0; bs
cmp -s "$PB" "$OUT/pb.srm" || bad "a server save of another size didn't replace a blank one: $(cat "$OUT/bs.log")"
[ "$(ls "$C/banana/save-vault/saves/Peel Blank (USA)/" 2>/dev/null | grep -c -- "-$(md5sum < "$OUT/pb-blank.srm" | cut -c1-8)\.srm$")" = 1 ] || bad "the blank it replaced isn't in the vault"
grep -q 'another size' "$OUT/bs.log" && bad "a blank save held the server's of another size: $(cat "$OUT/bs.log")"
# a game on the second SD card (the RG DS Plus's, while it is in): on the handheld as one on this card is, its save
# here in saves/ as any other's, so it goes each way, after a game of it too
mkdir -p "$OUT/sd2/roms/gba" "$C/run/sd2"; mv "$C/roms/gba/Second Card (USA).gba" "$OUT/sd2/roms/gba/"; echo "$OUT/sd2/roms" > "$C/run/sd2/roms"
head -c 8192 /dev/urandom > "$S/Second Card (USA).srm"; bs
[ "$(saves3 "Second Card")" = 1 ] || bad "a second-card game's save wasn't sent: $(cat "$OUT/bs.log")"
head -c 8192 /dev/urandom > "$OUT/sd2.srm"; seed "Second Card" "$(hex "$OUT/sd2.srm")" 5 >/dev/null; bs
cmp -s "$S/Second Card (USA).srm" "$OUT/sd2.srm" || bad "a second-card game's save didn't come down: $(cat "$OUT/bs.log")"
printf 'homesync_auto = quit\n' >> "$C/banana/settings.conf"
head -c 8192 /dev/urandom > "$S/Second Card (USA).srm"; bsq "$OUT/sd2/roms/gba/Second Card (USA).gba"
[ "$(saves3 "Second Card")" = 3 ] || bad "the sync after a second-card game didn't send its save: $(cat "$OUT/bs.log")"
# a second-card game played with the server out of reach, never synced from this card, and the card out when Wi-Fi is
# back: its save goes on waiting (with the card out none of its games can be matched with the server's, so it can't be
# said the server doesn't list it), and is decided once the card is in again
printf 'a game of the second card own' > "$OUT/sd2/roms/gba/Sd Own (USA).gba"; head -c 8192 /dev/urandom > "$S/Sd Own (USA).srm"
cp "$C/banana/romsync.json" "$OUT/sd2.json"; sed -i "s#\"server\": \"$U3\"#\"server\": \"http://127.0.0.1:1\"#" "$C/banana/romsync.json"
bsq "$OUT/sd2/roms/gba/Sd Own (USA).gba"
sed -i "s#\"server\": \"http://127.0.0.1:1\"#\"server\": \"$U3\"#" "$C/banana/romsync.json"
"$BIN" --root "$C" --run "$C/run" saves | grep -q '^queued	rom:gba/Sd Own (USA).gba	' || bad "a second-card game, the server out of reach: not waiting: $(cat "$OUT/bs.log")"
rm "$C/run/sd2/roms"
"$BIN" --root "$C" --run "$C/run" --status "$C/run/sync.status" sync --auto wifi > "$OUT/bs.log" 2>&1
"$BIN" --root "$C" --run "$C/run" saves | grep -q '^queued	rom:gba/Sd Own (USA).gba	' || bad "a second-card game's save stopped waiting while the card was out: $(cat "$OUT/bs.log")"
# (and with nothing else waiting, the return of Wi-Fi asks the server nothing for it: the card may be out for good)
[ ! -s "$OUT/bs.log" ] || bad "a save waiting for the second card, out, still had the server's games listed: $(cat "$OUT/bs.log")"
echo "$OUT/sd2/roms" > "$C/run/sd2/roms"
"$BIN" --root "$C" --run "$C/run" --status "$C/run/sync.status" sync --auto wifi > "$OUT/bs.log" 2>&1
grep -q 'no longer waiting to be sent, not a game the server lists.*gba/Sd Own (USA).gba' "$OUT/bs.log" || bad "a second-card game the server doesn't list still waits with the card back: $(cat "$OUT/bs.log")"
rm -f "$OUT/sd2/roms/gba/Sd Own (USA).gba" "$S/Sd Own (USA).srm"
sed -i '/^homesync_auto = quit/d' "$C/banana/settings.conf"
# the same name, another dump: not the server's game, its save not sent, and said
head -c 8192 /dev/urandom > "$S/Other Bytes (USA).srm"; bs
[ "$(saves3 "Other Bytes")" = 0 ] || bad "a save of another dump of the game was sent"
grep -q "not synced, not the server's ROM (other bytes): gba/Other Bytes (USA).gba" "$OUT/bs.log" && grep -q '1 game not matched on the server' "$OUT/bs.log" ||
    bad "another dump not said: $(cat "$OUT/bs.log")"
# a game whose saves stay on this handheld (banana/homesync-local.txt, its page's Save sync off): its save neither sent
# nor taken, the other games' as ever, and the sync after it done at once without asking the server
head -c 8192 /dev/urandom > "$S/Not Chosen (USA).srm"; head -c 8192 /dev/urandom > "$S/Zip Game (USA).srm"
printf '# kept here\ngba/Not Chosen (USA).gba\n' > "$C/banana/homesync-local.txt"; bs
[ "$(saves3 "Not Chosen")" = 0 ] && [ "$(nsaves "Zip Game (USA).zip")" = 1 ] || bad "a game kept here: sent $(saves3 "Not Chosen"), the other game's $(nsaves "Zip Game (USA).zip"): $(cat "$OUT/bs.log")"
grep -q 'not synced, its saves stay on this handheld: gba/Not Chosen (USA).gba' "$OUT/bs.log" || bad "a game kept here not said: $(cat "$OUT/bs.log")"
cp "$S/Not Chosen (USA).srm" "$OUT/nc1.srm"
head -c 8192 /dev/urandom > "$OUT/nc2.srm"; seed "Not Chosen" "$(hex "$OUT/nc2.srm")" 5 >/dev/null; bs
cmp -s "$S/Not Chosen (USA).srm" "$OUT/nc1.srm" || bad "a game kept here took the server's newer save: $(cat "$OUT/bs.log")"
"$BIN" --root "$C" --run "$C/run" --status "$C/run/sync.status" sync --rom "$C/roms/gba/Not Chosen (USA).gba" > "$OUT/bs.log" 2>&1
grep -q 'its saves stay on this handheld' "$OUT/bs.log" && [ "$(cat "$C/run/gameend.status")" = "done 0 0 0 0 0 0|its saves stay on this handheld" ] ||
    bad "the sync after a game kept here: $(cat "$C/run/gameend.status") $(cat "$OUT/bs.log")"
# (and the sync before it: done at once, nothing held for the menu to ask about, the server not asked)
printf 'homesync_auto = quit\nhomesync_launch = on\n' >> "$C/banana/settings.conf"; echo held > "$C/run/launch.conflict"
n=$(curl -s "$U3/test/log" | jget '.__len__()')	# (each look at the log is in it: the next one is one more)
"$BIN" --root "$C" --run "$C/run" --status "$C/run/sync.status" sync --auto launch --rom "$C/roms/gba/Not Chosen (USA).gba" > "$OUT/bs.log" 2>&1
[ ! -s "$C/run/launch.conflict" ] && grep -q '^done ' "$C/run/launch.status" && [ "$(curl -s "$U3/test/log" | jget '.__len__()')" = $((n + 1)) ] ||
    bad "the sync before a game kept here: $(cat "$C/run/launch.status" "$C/run/launch.conflict" "$OUT/bs.log")"
sed -i '/^homesync_auto = quit$/d; /^homesync_launch = on$/d' "$C/banana/settings.conf"
# (keyed by the folder on the card, as the game page writes it: a system whose folder systems.conf names otherwise
# keeps its game's saves here all the same)
sed 's/^\[gba\]$/[gba]\nfolder = gbx/' "$BANANA_SYSTEMS" > "$OUT/sys-gbx.conf"; mv "$C/roms/gba" "$C/roms/gbx"
printf '# kept here\ngbx/Not Chosen (USA).gba\n' > "$C/banana/homesync-local.txt"
# (its states too, with them on: the server's not taken into the vault)
curl -s -XPOST "$U3/test/save" -d "{\"kind\": \"states\", \"rom_id\": $(rid "Not Chosen"), \"file_name\": \"Not Chosen (USA).state\", \"data\": \"$(printf 'a state of the server' | od -An -v -tx1 | tr -d ' \n')\"}" >/dev/null
echo 'homesync_states = on' >> "$C/banana/settings.conf"
BANANA_SYSTEMS=$OUT/sys-gbx.conf bs
grep -q 'not synced, its saves stay on this handheld: gbx/Not Chosen (USA).gba' "$OUT/bs.log" && cmp -s "$S/Not Chosen (USA).srm" "$OUT/nc1.srm" ||
    bad "a game kept here, its system in a folder of another name: $(cat "$OUT/bs.log")"
grep -q 'Not Chosen (USA).state' "$OUT/bs.log" && bad "a game kept here, its system in a folder of another name: the server's state taken: $(grep 'Not Chosen' "$OUT/bs.log")"
sed -i '/^homesync_states = on$/d' "$C/banana/settings.conf"
mv "$C/roms/gbx" "$C/roms/gba"
rm -f "$C/banana/homesync-local.txt" "$S/Not Chosen (USA).srm" "$C/roms/gba/Not Chosen (USA).gba"
# a zipped game, the server's by the zip's members
bs; [ "$(nsaves "Zip Game (USA).zip")" = 1 ] || bad "a zipped game's save wasn't sent: $(cat "$OUT/bs.log")"
# another core's save of the same game (mGBA's, from before every GBA core shared saves/gba): kept in the vault once,
# said once, never sent, never removed
mkdir -p "$C/saves/mGBA"; head -c 8192 /dev/urandom > "$S/Two Cores (USA).srm"; head -c 8192 /dev/urandom > "$C/saves/mGBA/Two Cores (USA).srm"; bs
[ "$(vaulted "Two Cores" "$C/saves/mGBA/Two Cores (USA).srm" mGBA)" = 1 ] || bad "another core's save isn't in the vault"
grep -q 'also here, kept in the vault, not synced: saves/mGBA/Two Cores (USA).srm' "$OUT/bs.log" || bad "another core's save not said: $(cat "$OUT/bs.log")"
[ "$(saves3 "Two Cores")" = 1 ] && [ -e "$C/saves/mGBA/Two Cores (USA).srm" ] || bad "another core's save sent or removed: $(saves3 "Two Cores")"
bs; grep -q 'also here' "$OUT/bs.log" && bad "another core's save said again"
# a GBA game's save in the flat saves/ (a 0.5.0 download left one there; the GBA's cores read saves/gba): kept in the
# vault and said, left where it is (another system's game of the same name would read it), never sent
head -c 8192 /dev/urandom > "$C/saves/Flat (USA).srm"
bs
[ "$(vaulted Flat "$C/saves/Flat (USA).srm" .)" = 1 ] || bad "a flat 0.5.0 save isn't in the vault: $(find "$C/banana/save-vault" -path '*Flat*')"
grep -q 'an earlier download, kept in the vault, not synced: saves/Flat (USA).srm' "$OUT/bs.log" || bad "a flat 0.5.0 save not said: $(cat "$OUT/bs.log")"
[ -e "$C/saves/Flat (USA).srm" ] && [ "$(saves3 Flat)" = 0 ] || bad "a flat 0.5.0 save removed or sent"
# RetroArch's config still sorting saves by core (a card from before 0.5.3, until its next game: run-retroarch turns it
# off at every start): the flat saves/ synced all the same, the GBA's too
cp "$C/retroarch/retroarch.cfg" "$OUT/ra.cfg"; sed -i 's/^sort_savefiles_enable = .*/sort_savefiles_enable = "true"/' "$C/retroarch/retroarch.cfg"
head -c 8192 /dev/urandom > "$K"; head -c 8192 /dev/urandom > "$S/Sorted Off (USA).srm"; bs
kart() { srv3 "/api/saves?rom_id=$(ridf "Peel Kart (USA).sfc")" | python3 -c 'import json,sys; print(max(json.load(sys.stdin), key=lambda s: (s["updated_at"], s["id"]))["content_hash"])'; }
[ "$(saves3 "Sorted Off")" = 1 ] && [ "$(kart)" = "$(md5sum < "$K" | cut -c1-32)" ] || bad "sorting on in RetroArch's config: gba $(saves3 "Sorted Off"), the SNES one not sent: $(cat "$OUT/bs.log")"
cp "$OUT/ra.cfg" "$C/retroarch/retroarch.cfg"
# no core for the system: said, nothing written anywhere
seedf "Peel Kart (USA).sfc" "Peel Kart (USA).srm" "$(hex "$OUT/kart.srm")" 10; cp "$K" "$OUT/kart-here.srm"
BANANA_LIBRETRO=$OUT/no-cores bs
grep -q 'error: saves: snes: core snes9x isn.t installed' "$OUT/bs.log" || bad "no core not said: $(cat "$OUT/bs.log")"
cmp -s "$K" "$OUT/kart-here.srm" && [ "$(find "$C/saves" -name 'Peel Kart*' | wc -l)" = 1 ] || bad "a save written with no core to say where"
# the DS: a server .dsv (DraStic's: the raw save, then DeSmuME's footer; RomM's name for the version has its time in
# it) taken as its raw save, and agreed so: the next sync neither sends nor fetches it. A .sav likewise, DraStic's .dsv
# brought into step with each by nds-saves (the real one, on this card), which goes by content, never by the files'
# times. A card whose raw save is the .dsv's agrees with it at once; one of another size than the card's is held, kept
# in the vault, never put in place
NDS=$HERE/../system/rootfs/usr/libexec/banana/nds-saves
cat > "$OUT/nds-saves3" <<EOF
#!/bin/sh
echo "\$1" >> "$OUT/nds-saves3.log"
BANANA_ROOT=$C exec sh "$NDS" "\$@" >> "$OUT/nds-saves3.log" 2>&1
EOF
chmod +x "$OUT/nds-saves3"
Q=$C/saves/nds/"Peel Quest (USA).sav"; QD=$C/drastic/backup/"Peel Quest (USA).dsv"; QR=$C/banana/nds-saves/"Peel Quest (USA).md5"
QV=$C/banana/save-vault/saves/nds/"Peel Quest (USA)"
# qvault <file>: how many copies of the file's bytes the vault holds for the game (the same bytes kept once)
qvault() { ls "$QV" 2>/dev/null | grep -c -- "-$(md5sum < "$1" | cut -c1-8)\.sav$"; }
m() { md5sum < "$1" | cut -c1-32; }
head -c 8192 /dev/urandom > "$OUT/qd.sav"; dsv "$OUT/qd.sav" "$OUT/qd.dsv"; seedf "Peel Quest (USA).nds" "Peel Quest (USA).dsv" "$(hex "$OUT/qd.dsv")" 0
BANANA_NDS_SAVES=$OUT/nds-saves3 bs
cmp -s "$Q" "$OUT/qd.sav" || bad "a server .dsv not taken as its raw save: $(cat "$OUT/bs.log")"
[ -z "$(find "$C/saves" -name '*.dsv')" ] || bad "a server .dsv was put in saves/: $(find "$C/saves" -name '*.dsv')"
grep -qx "$C/roms/nds/Peel Quest (USA).nds" "$OUT/nds-saves3.log" 2>/dev/null || bad "nds-saves wasn't run after a .dsv was installed"
cmp -s "$QD" "$OUT/qd.sav" && [ "$(cat "$QR" 2>/dev/null)" = "$(m "$OUT/qd.sav")" ] || bad "DraStic's copy not made from the DS save installed, or the two not recorded as agreed: $(cat "$OUT/nds-saves3.log")"
BANANA_NDS_SAVES=$OUT/nds-saves3 bs
grep -q 'Peel Quest' "$OUT/bs.log" && bad "a .dsv taken once, sent or fetched again: $(cat "$OUT/bs.log")"
[ "$(nsaves "Peel Quest (USA).nds")" = 1 ] || bad "a .dsv taken, then the card's raw save sent back: $(nsaves "Peel Quest (USA).nds") versions"
# (DraStic's copy dated a minute ahead, as a card clock ahead of the server's leaves it: the save put in place is the
# one that changed from what the two last held, so DraStic takes it whatever the files' times say; the raw save
# replaced is in the vault once, though the sync and nds-saves each kept it)
head -c 8192 /dev/urandom > "$OUT/quest.sav"; seedf "Peel Quest (USA).nds" "Peel Quest (USA).sav" "$(hex "$OUT/quest.sav")" 5
touch -d "@$(( $(date +%s) + 60 ))" "$QD"
BANANA_NDS_SAVES=$OUT/nds-saves3 bs
cmp -s "$Q" "$OUT/quest.sav" || bad "a DS save from the server: $(cat "$OUT/bs.log")"
cmp -s "$QD" "$OUT/quest.sav" && [ "$(cat "$QR")" = "$(m "$OUT/quest.sav")" ] || bad "DraStic's copy, dated ahead, not brought into step with the DS save installed: $(cat "$OUT/nds-saves3.log")"
[ "$(qvault "$OUT/qd.sav")" = 1 ] || bad "the raw save replaced isn't in the vault once: $(ls "$QV")"
[ "$(grep -cx "$C/roms/nds/Peel Quest (USA).nds" "$OUT/nds-saves3.log")" -ge 2 ] || bad "nds-saves wasn't run after a DS save was installed"
head -c 32768 /dev/urandom > "$OUT/qbig.sav"; seedf "Peel Quest (USA).nds" "Peel Quest (USA).sav" "$(hex "$OUT/qbig.sav")" 10
BANANA_NDS_SAVES=$OUT/nds-saves3 bs
cmp -s "$Q" "$OUT/quest.sav" || bad "a DS save of another size than the card's put in place"
grep -q "not taken yet, another size than this handheld's save for the game: Peel Quest (USA) \[" "$OUT/bs.log" || bad "a DS save of another size not said: $(cat "$OUT/bs.log")"
ls "$C/banana/save-vault/saves/nds/Peel Quest (USA)/" | grep -q -- "-$(md5sum < "$OUT/qbig.sav" | cut -c1-8)\.sav$" || bad "a held DS save isn't in the vault"
# (held, then the next sync: the vault's copy looked at again, not fetched again)
qid=$(heldf "saves/nds/Peel Quest (USA).sav" save_id); BANANA_NDS_SAVES=$OUT/nds-saves3 bs
grep -q "not taken yet, another size than this handheld's save for the game: Peel Quest (USA) \[" "$OUT/bs.log" && cmp -s "$Q" "$OUT/quest.sav" && [ "$(gets "$qid")" = 1 ] ||
    bad "a held DS save, the next sync: fetched $(gets "$qid") times: $(cat "$OUT/bs.log")"
# (its copy in the vault changed there: not the server's save any more, so fetched again and kept again beside it)
qv=$(heldf "saves/nds/Peel Quest (USA).sav" vault)
printf x >> "$qv"; BANANA_NDS_SAVES=$OUT/nds-saves3 bs
grep -q "not taken yet, another size than this handheld's save for the game: Peel Quest (USA) \[" "$OUT/bs.log" && cmp -s "$Q" "$OUT/quest.sav" && [ "$(gets "$qid")" = 2 ] &&
    md5sum "$C/banana/save-vault/saves/nds/Peel Quest (USA)/"* | grep -q "^$(md5sum < "$OUT/qbig.sav" | cut -d' ' -f1) " ||
    bad "a held DS save whose vault copy was changed: fetched $(gets "$qid") times, or no good copy kept: $(cat "$OUT/bs.log")"
cp "$L3/nds/Peel Two (USA).nds" "$C/roms/nds/"; head -c 8192 /dev/urandom > "$C/saves/nds/Peel Two (USA).sav"; dsv "$C/saves/nds/Peel Two (USA).sav" "$OUT/two.dsv"
seedf "Peel Two (USA).nds" "Peel Two (USA).dsv" "$(hex "$OUT/two.dsv")" 0; BANANA_NDS_SAVES=$OUT/nds-saves3 bs
grep -q 'the server.s newest is this same save in another form: saves/nds/Peel Two' "$OUT/bs.log" && ! grep -q 'conflict, both kept: saves/nds/Peel Two' "$OUT/bs.log" ||
    bad "the card's raw save under a .dsv of the same bytes not agreed: $(cat "$OUT/bs.log")"
BANANA_NDS_SAVES=$OUT/nds-saves3 bs
grep -q 'Peel Two' "$OUT/bs.log" && bad "a .dsv agreed with, looked at again: $(cat "$OUT/bs.log")"
[ "$(nsaves "Peel Two (USA).nds")" = 1 ] || bad "a .dsv agreed with, then the card's raw save sent: $(nsaves "Peel Two (USA).nds") versions"
# nds-saves between the two emulators' saves, by content: DraStic's changed (played there, the card's clock since put
# back so BananaDS's file is dated a day ahead of it): BananaDS takes it, the save replaced kept in the vault, and the
# sync sends it as the card's
ndsrun() { "$OUT/nds-saves3" "$C/roms/nds/Peel Quest (USA).nds"; }
# (the slot's head first made the card's own save again, dated 20 s on, with the server's clock put 20 s on too so that
# everything the card sends after it is newer still; the one held above, of another size, stays a version under them)
curl -s -XPOST "$U3/test/clock" -d '{"offset_s": 20}' >/dev/null
seedf "Peel Quest (USA).nds" "Peel Quest (USA).sav" "$(hex "$Q")" 20; BANANA_NDS_SAVES=$OUT/nds-saves3 bs
head -c 8192 /dev/urandom > "$OUT/qdr.sav"; dsv "$OUT/qdr.sav" "$QD"; touch -d "@$(( $(date +%s) + 86400 ))" "$Q"
ndsrun; rc=$?
[ $rc = 0 ] && cmp -s "$Q" "$OUT/qdr.sav" && [ "$(cat "$QR")" = "$(m "$OUT/qdr.sav")" ] || bad "DraStic's changed save, BananaDS's dated ahead: not taken ($rc): $(tail -2 "$OUT/nds-saves3.log")"
[ "$(qvault "$OUT/quest.sav")" = 1 ] || bad "the save BananaDS had isn't in the vault: $(ls "$QV")"
[ -e "$Q.bak" ] && bad "nds-saves still keeps a .bak beside the save"
n=$(nsaves "Peel Quest (USA).nds"); BANANA_NDS_SAVES=$OUT/nds-saves3 bs
[ "$(nsaves "Peel Quest (USA).nds")" = $((n + 1)) ] && [ "$(srv3 "/api/saves?rom_id=$(ridf "Peel Quest (USA).nds")&slot=autosave" | python3 -c 'import json,sys; print(max(json.load(sys.stdin), key=lambda s: (s["updated_at"], s["id"]))["content_hash"])')" = "$(m "$OUT/qdr.sav")" ] ||
    bad "DraStic's save, taken by BananaDS, not sent: $(cat "$OUT/bs.log")"
# both changed since they last agreed (one played in each without a start in between, a save edited by hand): neither
# touched, said, and the sync leaves the game alone until the game's next start, where the emulator starting takes its
# own (nds-saves --prefer), the other kept in the vault
head -c 8192 /dev/urandom > "$OUT/qboth.sav"; dsv "$OUT/qboth.sav" "$QD"; head -c 8192 /dev/urandom > "$Q"; cp "$Q" "$OUT/qsav.sav"
ndsrun; rc=$?
[ $rc = 3 ] && cmp -s "$Q" "$OUT/qsav.sav" && [ "$(cat "$QR")" = "$(m "$OUT/qdr.sav")" ] && grep -q "both changed since they last agreed: neither touched" "$OUT/nds-saves3.log" ||
    bad "both DS saves changed: not left alone ($rc): $(tail -2 "$OUT/nds-saves3.log")"
raw_of_dsv "$QD" "$OUT/qd-now.sav"; cmp -s "$OUT/qd-now.sav" "$OUT/qboth.sav" || bad "both DS saves changed: DraStic's touched"
n=$(nsaves "Peel Quest (USA).nds"); BANANA_NDS_SAVES=$OUT/nds-saves3 bs
grep -q "not synced, DraStic's and BananaDS's saves both changed since they last agreed: nds/Peel Quest (USA).nds" "$OUT/bs.log" && [ "$(nsaves "Peel Quest (USA).nds")" = "$n" ] &&
    cmp -s "$Q" "$OUT/qsav.sav" || bad "both DS saves changed: the sync didn't leave the game alone: $(cat "$OUT/bs.log")"
# (listed for the Saves page with nothing to choose, the server's refused over it; the game's next start in BananaDS
# takes BananaDS's, DraStic's given it and the one it had kept in the vault, and the sync sends it)
"$BIN" --root "$C" --run "$C/run" saves > "$OUT/qlist" 2>&1
grep -q "^held	[0-9]*/autosave	saves/nds/Peel Quest (USA).sav	DraStic's and BananaDS's saves both changed.*	none	" "$OUT/qlist" || bad "both DS saves changed: not listed, or offers a choice: $(cat "$OUT/qlist")"
QK=$(grep "Peel Quest" "$OUT/qlist" | grep "^held" | cut -f2)
HK=$(grep "^held" "$OUT/qlist" | grep "size\|format" | head -1 | cut -f2)     # (held for another reason: no choice to make)
[ -n "$HK" ] && "$BIN" --root "$C" --run "$C/run" resolve "$HK" server | grep -q "^error not a conflict: " && "$BIN" --root "$C" --run "$C/run" resolve "$HK" card | tail -1 | grep -q "^error not a conflict: " ||
    bad "a save held for its format or size settled as a conflict: $(cat "$OUT/qlist")"
[ "$(BANANA_NDS_SAVES=$OUT/nds-saves3 "$BIN" --root "$C" --run "$C/run" resolve "$QK" server)" = "error DraStic's and BananaDS's saves differ: start the game first" ] || bad "both DS saves changed: settled as a server's"
# (a conflict the last sync left on the same game, its DS saves apart since: listed with nothing to choose and the DS
# line, on the page and before the game, as neither choice could be carried out until the game's next start)
cp "$C/banana/romsync.json" "$OUT/qconf.json"
python3 -c 'import json, sys; c = json.load(open(sys.argv[1])); n = dict(c["held"][sys.argv[2]]); n["why"] = ""; c["conflicts"][sys.argv[2]] = n; json.dump(c, open(sys.argv[1], "w"))' "$C/banana/romsync.json" "$QK"
for r in "" "nds/Peel Quest (USA).nds"; do
    "$BIN" --root "$C" --run "$C/run" saves ${r:+--rom "$r"} | grep -q "^conflict	$QK	saves/nds/Peel Quest (USA).sav	.*	none	[^	]*	ds$" ||
        bad "a conflict over DS saves apart offers a choice (${r:-the page}): $("$BIN" --root "$C" --run "$C/run" saves ${r:+--rom "$r"} | grep Quest)"
done
for how in card server; do
    BANANA_NDS_SAVES=$OUT/nds-saves3 "$BIN" --root "$C" --run "$C/run" resolve "$QK" $how | tail -1 | grep -qx "error DraStic's and BananaDS's saves differ: start the game first" ||
        bad "a conflict over DS saves apart: settled as $how"
done
cp "$OUT/qconf.json" "$C/banana/romsync.json"
"$OUT/nds-saves3" "$C/roms/nds/Peel Quest (USA).nds" --prefer sav; rc=$?
[ $rc = 0 ] && cmp -s "$Q" "$OUT/qsav.sav" && cmp -s "$QD" "$OUT/qsav.sav" && [ "$(qvault "$OUT/qboth.sav")" = 1 ] ||
    bad "both DS saves changed, BananaDS starting: its own not taken, or DraStic's not kept ($rc): $(tail -2 "$OUT/nds-saves3.log")"
BANANA_NDS_SAVES=$OUT/nds-saves3 bs
[ "$(nsaves "Peel Quest (USA).nds")" = $((n + 1)) ] && [ "$(cat "$QR")" = "$(m "$OUT/qsav.sav")" ] || bad "the DS saves brought together at a start: not sent: $(cat "$OUT/bs.log")"
# (only BananaDS's changed, DraStic starting: DraStic still takes BananaDS's, its own being the one they agreed)
head -c 8192 /dev/urandom > "$Q"; cp "$Q" "$OUT/qs3.sav"
"$OUT/nds-saves3" "$C/roms/nds/Peel Quest (USA).nds" --prefer dsv; rc=$?
[ $rc = 0 ] && cmp -s "$Q" "$OUT/qs3.sav" && cmp -s "$QD" "$OUT/qs3.sav" || bad "only BananaDS's changed, DraStic starting: DraStic's old one taken over it ($rc): $(tail -2 "$OUT/nds-saves3.log")"
# (both changed again, DraStic starting: DraStic's taken, BananaDS's kept; and a .dsv whose footer doesn't add up is
# no choice to make: neither touched)
head -c 8192 /dev/urandom > "$OUT/qd2.sav"; dsv "$OUT/qd2.sav" "$QD"; head -c 8192 /dev/urandom > "$Q"; cp "$Q" "$OUT/qs2.sav"
"$OUT/nds-saves3" "$C/roms/nds/Peel Quest (USA).nds" --prefer dsv; rc=$?
[ $rc = 0 ] && cmp -s "$Q" "$OUT/qd2.sav" && [ "$(qvault "$OUT/qs2.sav")" = 1 ] || bad "both DS saves changed, DraStic starting: its own not taken, or BananaDS's not kept ($rc)"
dsv "$OUT/qd2.sav" "$QD" version; head -c 8192 /dev/urandom > "$Q"; cp "$Q" "$OUT/qs4.sav"
"$OUT/nds-saves3" "$C/roms/nds/Peel Quest (USA).nds" --prefer sav; rc=$?
[ $rc = 5 ] && cmp -s "$Q" "$OUT/qs4.sav" || bad "a start over a .dsv whose footer doesn't add up: one taken ($rc)"
# (a kept copy with DraStic's footer put back: cut to the raw save, as a download would be)
dsv "$OUT/qd2.sav" "$QV/20200101T000000Z-feedface.sav"; cat "$OUT/qd2.sav" > "$QD"; ndsrun
[ "$("$BIN" --root "$C" --run "$C/run" restore "$QV/20200101T000000Z-feedface.sav" | tail -1)" = ok ] && cmp -s "$Q" "$OUT/qd2.sav" || bad "a kept .dsv restored with its footer"
BANANA_NDS_SAVES=$OUT/nds-saves3 bs
# a .dsv whose footer doesn't add up, or of a size no DS save has: neither touched (DraStic's may hold what BananaDS's
# doesn't), said with its own exit, and the sync holds the game, saying why, with nothing to choose
dsv "$OUT/qboth.sav" "$QD" version; head -c 8192 /dev/urandom > "$Q"; cp "$Q" "$OUT/qsav.sav"; cp "$QD" "$OUT/qbad.dsv"
ndsrun; rc=$?
[ $rc = 5 ] && cmp -s "$Q" "$OUT/qsav.sav" && cmp -s "$QD" "$OUT/qbad.dsv" && grep -q "DraStic's save can't be read" "$OUT/nds-saves3.log" || bad "a .dsv whose footer doesn't add up ($rc): $(tail -2 "$OUT/nds-saves3.log")"
n=$(nsaves "Peel Quest (USA).nds"); BANANA_NDS_SAVES=$OUT/nds-saves3 bs
grep -q "not synced, DraStic's save can't be read (left for its owner): nds/Peel Quest (USA).nds" "$OUT/bs.log" && [ "$(nsaves "Peel Quest (USA).nds")" = "$n" ] && cmp -s "$Q" "$OUT/qsav.sav" &&
    "$BIN" --root "$C" --run "$C/run" saves | grep -q "^held	[0-9]*/autosave	saves/nds/Peel Quest (USA).sav	DraStic's save can't be read	.*	none	[^	]*	unread$" ||
    bad "DraStic's save unreadable: the sync didn't hold the game, or offers a choice: $(cat "$OUT/bs.log") $("$BIN" --root "$C" --run "$C/run" saves | grep Quest)"
head -c 8000 /dev/urandom > "$QD"; ndsrun; rc=$?
[ $rc = 5 ] && cmp -s "$Q" "$OUT/qsav.sav" || bad "a .dsv of a size no DS save has ($rc): $(tail -2 "$OUT/nds-saves3.log")"
# the record of what the two last held there but unreadable (a power cut while it was written): held as both changed,
# not decided by the files' times
dsv "$OUT/qboth.sav" "$QD"; cp "$QR" "$OUT/qr.md5"; : > "$QR"; ndsrun; rc=$?
[ $rc = 3 ] && cmp -s "$Q" "$OUT/qsav.sav" && grep -q "record of the save the two last held can't be read" "$OUT/nds-saves3.log" || bad "an empty record ($rc): $(tail -2 "$OUT/nds-saves3.log")"
echo abc > "$QR"; ndsrun; rc=$?; [ $rc = 3 ] || bad "a malformed record ($rc)"
# (never stuck there: a start of the game writes it again, its emulator's own taken, as the two agreeing does)
"$OUT/nds-saves3" "$C/roms/nds/Peel Quest (USA).nds" --prefer sav; rc=$?
[ $rc = 0 ] && cmp -s "$Q" "$OUT/qsav.sav" && cmp -s "$QD" "$OUT/qsav.sav" && [ "$(cat "$QR")" = "$(m "$OUT/qsav.sav")" ] || bad "a start over an unreadable record ($rc): $(tail -2 "$OUT/nds-saves3.log")"
: > "$QR"; ndsrun; rc=$?
[ $rc = 0 ] && [ "$(cat "$QR")" = "$(m "$OUT/qsav.sav")" ] || bad "an unreadable record, the two agreeing, not written again ($rc)"
# a save that can't be read (md5sum failing on it) isn't one that isn't there: nothing written over it, exit 6
mkdir -p "$OUT/nomd5"; R5=$(command -v md5sum)
printf '#!/bin/sh\ncase "$1" in *.sav) exit 1 ;; esac\nexec %s "$@"\n' "$R5" > "$OUT/nomd5/md5sum"; chmod +x "$OUT/nomd5/md5sum"
head -c 8192 /dev/urandom > "$OUT/qother.sav"; dsv "$OUT/qother.sav" "$QD"
PATH=$OUT/nomd5:$PATH ndsrun; rc=$?
[ $rc = 6 ] && cmp -s "$Q" "$OUT/qsav.sav" || bad "an unreadable .sav written over ($rc): $(tail -2 "$OUT/nds-saves3.log")"
# a write that fails says so, so the sync doesn't take the two as agreed: the record's folder a file here
cat "$Q" > "$QD"; mv "$C/banana/nds-saves" "$OUT/rec.saved"; : > "$C/banana/nds-saves"; ndsrun; rc=$?; rm "$C/banana/nds-saves"; mv "$OUT/rec.saved" "$C/banana/nds-saves"
[ $rc = 6 ] || bad "a failed write of the record exited $rc"
# what a cut run left beside the files is cleared, and was never a save
: > "$Q.nds-saves.999.tmp"; : > "$QD.nds-saves.999.tmp"; mkdir -p "$C/banana/save-vault/saves/nds/Peel Quest (USA)"; : > "$C/banana/save-vault/saves/nds/Peel Quest (USA)/x-aaaaaaaa.sav.nds-saves.999.tmp"; ndsrun
ls "$C/saves/nds" "$C/drastic/backup" "$C/banana/save-vault/saves/nds/Peel Quest (USA)" | grep -q '\.tmp$' && bad "a cut run's file not cleared"
# (one a run still going left, a launcher's beside a sync's, is that run's own)
: > "$Q.nds-saves.$$.tmp"; ndsrun; [ -e "$Q.nds-saves.$$.tmp" ] || bad "a running nds-saves' file removed by another"; rm -f "$Q.nds-saves.$$.tmp"
# the vault unable to keep the save to be replaced: neither touched, a failure (6, a start's too), not two that both
# changed, which would offer a choice for a pair that never diverged
NV="$C/banana/save-vault/saves/nds/Peel Quest (USA)"; mv "$NV" "$OUT/nv.saved"; : > "$NV"
head -c 8192 /dev/urandom > "$OUT/qkeep.sav"; dsv "$OUT/qkeep.sav" "$QD"; ndsrun; rc=$?
[ $rc = 6 ] && cmp -s "$Q" "$OUT/qsav.sav" || bad "a keep that failed exited $rc: $(tail -2 "$OUT/nds-saves3.log")"
"$OUT/nds-saves3" "$C/roms/nds/Peel Quest (USA).nds" --prefer dsv; rc=$?
[ $rc = 6 ] && cmp -s "$Q" "$OUT/qsav.sav" || bad "a start whose keep failed exited $rc"
rm "$NV"; mv "$OUT/nv.saved" "$NV"
dsv "$OUT/qboth.sav" "$QD"; cp "$OUT/qr.md5" "$QR"
# a DS pair that can't be brought together, out of reach: kept, not left waiting, the save to send settled at the game's next start
printf '#!/bin/sh\nexit 3\n' > "$OUT/nds-three"; chmod +x "$OUT/nds-three"; cp "$C/banana/romsync.json" "$OUT/rm2.json"
sed "s#\"server\": \"$U3\"#\"server\": \"http://127.0.0.1:1\"#" "$OUT/rm2.json" > "$C/banana/romsync.json"
cp "$C/banana/settings.conf" "$OUT/settings.saved"; printf 'homesync_auto = quit\n' >> "$C/banana/settings.conf"
BANANA_NDS_SAVES=$OUT/nds-three bsq "nds/Peel Quest (USA).nds"
cp "$OUT/settings.saved" "$C/banana/settings.conf"
grep -q 'kept in the vault, the server out of reach: saves/nds/Peel Quest' "$OUT/bs.log" || bad "a held DS pair out of reach: $(cat "$OUT/bs.log")"
python3 -c 'import json, sys; sys.exit(1 if any("nds" in v["vault"] for v in json.load(open(sys.argv[1]))["queue"].values()) else 0)' "$C/banana/romsync.json" || bad "a held DS pair left waiting to be sent"
# (DraStic's too, as what was played may be its)
ls "$NV" | grep -q -- "-$(m "$QD" | cut -c1-8)\.sav$" || bad "a held DS pair out of reach: DraStic's save not kept: $(ls "$NV")"
cp "$OUT/rm2.json" "$C/banana/romsync.json"
# the two not brought together (a read or a write failed): that run leaves the game, nothing sent from a save that may
# be the older, and the next run tries again
printf '#!/bin/sh\nexit 6\n' > "$OUT/nds-six"; chmod +x "$OUT/nds-six"
head -c 8192 /dev/urandom > "$Q"; n=$(nsaves "Peel Quest (USA).nds"); BANANA_NDS_SAVES=$OUT/nds-six bs
grep -q "not synced, DraStic's and BananaDS's saves couldn't be brought together.*: nds/Peel Quest (USA).nds" "$OUT/bs.log" && [ "$(nsaves "Peel Quest (USA).nds")" = "$n" ] ||
    bad "the DS pair not brought together, synced all the same: $(cat "$OUT/bs.log")"
cp "$OUT/qsav.sav" "$Q"
ls "$C/saves/nds" "$C/drastic/backup" "$C/banana/nds-saves" | grep -q 'nds-saves\.' && bad "nds-saves left a file of its own behind: $(ls "$C/saves/nds" "$C/drastic/backup" "$C/banana/nds-saves")"
# no record of what the two last held (a card from before the record, at whichever run meets the two first: here a
# sync's, with no emulator starting): the newer file's taken, this once, the other kept in the vault
cat "$Q" > "$QD"; ndsrun
head -c 8192 /dev/urandom > "$OUT/qnew.sav"; dsv "$OUT/qnew.sav" "$QD"; touch -d "@$(( $(date +%s) - 60 ))" "$Q"; rm "$QR"
ndsrun; rc=$?
[ $rc = 0 ] && cmp -s "$Q" "$OUT/qnew.sav" && [ "$(cat "$QR")" = "$(m "$OUT/qnew.sav")" ] && [ "$(qvault "$OUT/qsav.sav")" = 1 ] ||
    bad "the two DS saves with no record of what they last held ($rc): $(tail -2 "$OUT/nds-saves3.log")"
BANANA_NDS_SAVES=$OUT/nds-saves3 bs
# a game started while a DS save was being placed (nds-saves, which runs right after, stands for it): the save is in
# place, but not agreed, so the next sync looks again and agrees on it without fetching or sending
cat > "$OUT/nds-saves-game" <<EOF
#!/bin/sh
# (the run before the download finds the save as it was; the one after, the server's bytes placed: a game starts then)
if cmp -s "$Q" "$OUT/qgame.sav"; then sleep 30 & echo \$! > "$C/run/game.pid"; fi
exec "$OUT/nds-saves3" "\$@"
EOF
chmod +x "$OUT/nds-saves-game"
head -c 8192 /dev/urandom > "$OUT/qgame.sav"; seedf "Peel Quest (USA).nds" "Peel Quest (USA).sav" "$(hex "$OUT/qgame.sav")" 25	# (past the server's clock, 20 s on)
s=$(python3 -c 'import json,sys; print(json.dumps(json.load(open(sys.argv[1]))["slots"], sort_keys=True))' "$C/banana/romsync.json")
BANANA_NDS_SAVES=$OUT/nds-saves-game bs
kill "$(cat "$C/run/game.pid")"; rm "$C/run/game.pid"
grep -q 'Peel Quest (USA).sav: a game started while it was placed, decided next time' "$OUT/bs.log" && cmp -s "$Q" "$OUT/qgame.sav" &&
    [ "$(python3 -c 'import json,sys; print(json.dumps(json.load(open(sys.argv[1]))["slots"], sort_keys=True))' "$C/banana/romsync.json")" = "$s" ] ||
    bad "a game started while a DS save was placed: $(cat "$OUT/bs.log")"
n=$(nsaves "Peel Quest (USA).nds"); BANANA_NDS_SAVES=$OUT/nds-saves3 bs
[ "$(nsaves "Peel Quest (USA).nds")" = "$n" ] && ! grep -q 'Peel Quest' "$OUT/bs.log" && cmp -s "$Q" "$OUT/qgame.sav" ||
    bad "a save placed under a game that started, the next sync: $(cat "$OUT/bs.log")"
# DraStic's save not brought into step after a DS save was placed (nds-saves finds the two changed, as a DraStic that
# wrote meanwhile leaves them): in place, said, not agreed; the next sync, the two in step, agrees without a transfer
cat > "$OUT/nds-saves-out" <<EOF
#!/bin/sh
# (the second run for Peel Quest is the one after the download)
n=0; case "\$1" in *"Peel Quest"*) n=\$(cat "$OUT/nds-out.n" 2>/dev/null || echo 0); echo \$((n + 1)) > "$OUT/nds-out.n" ;; esac
"$OUT/nds-saves3" "\$@"; rc=\$?
[ "\$n" = 1 ] && exit 3
exit \$rc
EOF
chmod +x "$OUT/nds-saves-out"; rm -f "$OUT/nds-out.n"
head -c 8192 /dev/urandom > "$OUT/qout.sav"; seedf "Peel Quest (USA).nds" "Peel Quest (USA).sav" "$(hex "$OUT/qout.sav")" 30
slotmd5() { python3 -c 'import json,sys; s = json.load(open(sys.argv[1]))["slots"]; print(any(v["md5"] == sys.argv[2] and v["path"] == "saves/nds/Peel Quest (USA).sav" for v in s.values()))' "$C/banana/romsync.json" "$(m "$1")"; }
BANANA_NDS_SAVES=$OUT/nds-saves-out bs
grep -q "Peel Quest (USA).sav: DraStic's save isn't in step with it, decided next time" "$OUT/bs.log" && cmp -s "$Q" "$OUT/qout.sav" && [ "$(slotmd5 "$OUT/qout.sav")" = False ] ||
    bad "a DS save placed with DraStic's not in step: $(cat "$OUT/bs.log")"
n=$(nsaves "Peel Quest (USA).nds"); qid=$(srv3 "/api/saves?rom_id=$(ridf "Peel Quest (USA).nds")&slot=autosave" | python3 -c 'import json,sys; print(max(json.load(sys.stdin), key=lambda s: (s["updated_at"], s["id"]))["id"])'); g=$(gets "$qid")
BANANA_NDS_SAVES=$OUT/nds-saves3 bs
[ "$(nsaves "Peel Quest (USA).nds")" = "$n" ] && [ "$(gets "$qid")" = "$g" ] && [ "$(slotmd5 "$OUT/qout.sav")" = True ] && ! grep -q 'error:' "$OUT/bs.log" ||
    bad "the sync after DraStic's save came into step: versions $n then $(nsaves "Peel Quest (USA).nds"), fetched $g then $(gets "$qid"), agreed $(slotmd5 "$OUT/qout.sav"): $(cat "$OUT/bs.log")"
curl -s -XPOST "$U3/test/clock" -d '{"offset_s": 0}' >/dev/null

# the GBA: Grout's "<rom>.gba.sav" taken as the GBA save (agreed at once when it is the card's very bytes), gpSP's
# 128 KiB over mGBA's 32 KiB, and standalone mGBA's with the game's clock after it, taken without the clock; held, kept in the vault, never put in place: 32 KiB over 128 KiB that holds data past
# them, a zip; a file larger than any GBA save held without even being fetched
held() { python3 -c 'import json, sys; h = json.load(open(sys.argv[1]))["held"]; sys.exit(0 if any(v["path"] == sys.argv[2] and v["vault"] for v in h.values()) else 1)' "$C/banana/romsync.json" "saves/gba/$1 (USA).srm"; }
head -c 8192 /dev/urandom > "$S/Grout Same (USA).srm"; seedg "Grout Same" "Grout Same (USA).gba.sav" "$S/Grout Same (USA).srm" >/dev/null
head -c 8192 /dev/urandom > "$OUT/gn.sav"; seedg "Grout New" "Grout New (USA).gba.sav" "$OUT/gn.sav" >/dev/null
head -c 32768 /dev/urandom > "$S/Padded (USA).srm"; head -c 131072 /dev/urandom > "$S/Tail (USA).srm"; cp "$S/Tail (USA).srm" "$OUT/tail.srm"
head -c 8192 /dev/urandom > "$OUT/zipped.zip"; seedg Zipped "Zipped (USA).zip" "$OUT/zipped.zip" >/dev/null
head -c 262144 /dev/urandom > "$OUT/big.srm"; big=$(seedg "Too Big" "Too Big (USA).srm" "$OUT/big.srm")
head -c 131072 /dev/urandom > "$OUT/clock.srm"; cp "$OUT/clock.srm" "$OUT/clock.sav"; head -c 16 /dev/urandom >> "$OUT/clock.sav"; seedg Clock "Clock (USA).sav" "$OUT/clock.sav" >/dev/null
bs
cmp -s "$S/Clock (USA).srm" "$OUT/clock.srm" || bad "standalone mGBA's save not taken without its clock: $(cat "$OUT/bs.log")"
[ "$(python3 -c 'import json, sys; print(next((v.get("server_md5") for v in json.load(open(sys.argv[1]))["slots"].values() if v["path"] == "saves/gba/Clock (USA).srm"), None))' "$C/banana/romsync.json")" = "$(md5sum < "$OUT/clock.sav" | cut -d' ' -f1)" ] ||
    bad "standalone mGBA's save taken without its clock: the server's MD5 for it not kept"
grep -q 'Grout Same' "$OUT/bs.log" && bad "Grout's .gba.sav of the card's bytes not agreed at once: $(cat "$OUT/bs.log")"
[ "$(saves3 "Grout Same")" = 1 ] || bad "Grout's .gba.sav of the card's bytes: the card's sent as well"
cmp -s "$S/Grout New (USA).srm" "$OUT/gn.sav" || bad "Grout's .gba.sav not taken: $(cat "$OUT/bs.log")"
[ "$(saves3 Padded)" = 1 ] && [ "$(saves3 Tail)" = 1 ] || bad "the card's own GBA saves not sent: $(cat "$OUT/bs.log")"
[ -e "$S/Zipped (USA).srm" ] || [ -e "$S/Too Big (USA).srm" ] && bad "a zip, or a file larger than any GBA save, put in place"
grep -q "not taken yet, a format this handheld doesn't read: Zipped (USA) \[.*\]\.zip" "$OUT/bs.log" && held Zipped || bad "a zip not held with a vault copy: $(cat "$OUT/bs.log")"
grep -q "not taken yet, larger than any save of its system: Too Big (USA)" "$OUT/bs.log" || bad "a file larger than any GBA save not said: $(cat "$OUT/bs.log")"
[ "$(gets "$big")" = 0 ] ||
    bad "a file larger than any GBA save was fetched"
head -c 131072 /dev/urandom > "$OUT/padded.srm"; seedg Padded "Padded (USA).srm" "$OUT/padded.srm" 5 >/dev/null
head -c 32768 /dev/urandom > "$OUT/tail32.srm"; seedg Tail "Tail (USA).srm" "$OUT/tail32.srm" 5 >/dev/null
bs
grep -q 'Clock' "$OUT/bs.log" && bad "a GBA save taken without its clock, sent or fetched again: $(cat "$OUT/bs.log")"
cmp -s "$S/Padded (USA).srm" "$OUT/padded.srm" || bad "gpSP's 128 KiB over mGBA's 32 KiB not taken: $(cat "$OUT/bs.log")"
cmp -s "$S/Tail (USA).srm" "$OUT/tail.srm" || bad "32 KiB put over 128 KiB that holds data past them"
grep -q "not taken yet, another size than this handheld's save for the game: Tail (USA)" "$OUT/bs.log" && held Tail || bad "32 KiB over 128 KiB of data not held with a vault copy: $(cat "$OUT/bs.log")"
grep -q 'saves held back' "$OUT/bs.log" || bad "the held saves not counted: $(cat "$OUT/bs.log")"
tid=$(heldf "saves/gba/Tail (USA).srm" save_id); bs
grep -q "not taken yet, another size than this handheld's save for the game: Tail (USA)" "$OUT/bs.log" && held Tail && [ "$(gets "$tid")" = 1 ] ||
    bad "32 KiB held over 128 KiB of data, the next sync: fetched $(gets "$tid") times: $(cat "$OUT/bs.log")"

# (nothing the card sent weakened the server's guards)
[ "$(curl -s "$U3/test/violations")" = "[]" ] || bad "the card's sync sent what it never may: $(curl -s "$U3/test/violations")"

# ---------------------------------------------------------------- handhelds sharing RomM's slots: each game's save is
# its ROM's autosave slot, and which way it goes is decided by content alone, against the version both sides last held;
# a conflict is held with both kept, a save gone from either side is never deleted or refilled, and RomM's guards are
# never weakened (every card's token strict)
L4=$OUT/lib4
mkdir -p "$L4/gba" "$L4/nds"
for g in Shared Gone "Blank Head" Skew Dry Joiner Lone Race "Race Same" "Race Size" Queued "Queued Gone" Refetch Kept Diverged Closed Solo Other Device; do printf 'gba rom %s' "$g" > "$L4/gba/$g (USA).gba"; done
printf 'nds rom' > "$L4/nds/Peel Quest (USA).nds"; printf 'nds rom race' > "$L4/nds/Race Dsv (USA).nds"
P4=$(port)
python3 "$HERE/tests/fake-romm.py" "$P4" "$L4" 2> "$OUT/fake-romm4.log" & PIDS="$PIDS $!"
waitport "$P4" /api/heartbeat || bad "the slots' fake RomM didn't start"
U4=http://127.0.0.1:$P4
fakecore "$OUT/cores/gpsp_libretro.so" || bad "the GBA test core can't be put in place"
TA4=$(curl -s -XPOST "$U4/test/token" -d '{}' | jget '["token"]')
s4() { curl -s -H "Authorization: Bearer $TA4" "$U4$1"; }
TOKS4=""
# hand <name> [<token's scopes as JSON>]: a handheld paired with the server (its device registered at its first sync)
hand() {
    _h=$OUT/h-$1; realcard "$_h"; cp "$L4/gba/"* "$_h/roms/gba/"; cp "$L4/nds/"* "$_h/roms/nds/"
    _b=${2:-}; [ -n "$_b" ] || _b='{}'
    _t=$(curl -s -XPOST "$U4/test/token" -d "$_b" | jget '["token"]')
    TOKS4="$TOKS4${TOKS4:+, }\"$_t\""; curl -s -XPOST "$U4/test/strict" -d "{\"tokens\": [$TOKS4]}" >/dev/null
    printf '{"backend": "romm", "server": "%s", "token": "%s"}\n' "$U4" "$_t" > "$_h/banana/romsync.json"
    cp "$_h/banana/romsync.json" "$OUT/h-$1.first.json"
}
hs() { _n=$1; shift; "$BIN" --root "$OUT/h-$_n" --run "$OUT/h-$_n/run" --status "$OUT/h-$_n/run/sync.status" sync --saves-only "$@" > "$OUT/hs-$_n.log" 2>&1; }
rid4() { for p in $(s4 /api/platforms | python3 -c 'import json,sys; print(" ".join(str(p["id"]) for p in json.load(sys.stdin)))'); do
    s4 "/api/roms?platform_ids=$p&limit=500" | python3 -c "import json,sys; [print(r['id']) for r in json.load(sys.stdin)['items'] if r['fs_name'] == sys.argv[1]]" "$1"; done; }
# head4 <ROM file> [<device>]: the slot's head as "<MD5> <emulator> <id> <current for the device>", "none" without one
head4() { s4 "/api/saves?rom_id=$(rid4 "$1")&slot=autosave${2:+&device_id=$2}" | python3 -c '
import json, sys
rows = json.load(sys.stdin)
h = max(rows, key=lambda s: (s["updated_at"], s["id"])) if rows else None
print(" ".join([h["content_hash"], h["emulator"], str(h["id"]), str(h["device_syncs"][0]["is_current"] if h["device_syncs"] else "-")]) if h else "none")'; }
nv4() { s4 "/api/saves?rom_id=$(rid4 "$1")" | jget '.__len__()'; }
m() { md5sum < "$1" | cut -c1-32; }
devof4() { python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["device"])' "$OUT/h-$1/banana/romsync.json"; }
logc() { s4 /test/log | python3 -c 'import json,sys; print(sum(1 for x in json.load(sys.stdin) if x.startswith(sys.argv[1])))' "$1"; }
rs() { python3 -c "import json,sys; c = json.load(open(sys.argv[1])); print($2)" "$OUT/h-$1/banana/romsync.json"; }
hand a; hand b
GA=$OUT/h-a/saves/gba GB=$OUT/h-b/saves/gba
# up from A (every GBA core's saves/gba as the core it runs, BananaDS's saves/nds as melonDS's with either DS
# emulator; the .bak and quick-resume files beside them never), down to B, which RomM then has as having seen them
head -c 8192 /dev/urandom > "$GA/Shared (USA).srm"; head -c 8192 /dev/urandom > "$OUT/h-a/saves/nds/Peel Quest (USA).sav"
echo bak > "$GA/Shared (USA).srm.bak"; echo resume > "$OUT/h-a/saves/nds/Peel Quest (USA).resume.sav"
hs a
grep -q " 2 saves up" "$OUT/hs-a.log" || bad "slots: up from A: $(cat "$OUT/hs-a.log")"
s4 /test/state | grep -q 'resume\|\.bak' && bad "slots: a .bak or quick-resume file was sent"
[ "$(head4 "Shared (USA).gba" | cut -d' ' -f1-2)" = "$(m "$GA/Shared (USA).srm") gpsp" ] || bad "slots: the GBA head: $(head4 "Shared (USA).gba")"
[ "$(head4 "Peel Quest (USA).nds" | cut -d' ' -f2)" = melonds ] || bad "slots: the DS head: $(head4 "Peel Quest (USA).nds")"
hs b
cmp -s "$GB/Shared (USA).srm" "$GA/Shared (USA).srm" && cmp -s "$OUT/h-b/saves/nds/Peel Quest (USA).sav" "$OUT/h-a/saves/nds/Peel Quest (USA).sav" ||
    bad "slots: B didn't get A's saves: $(cat "$OUT/hs-b.log")"
[ "$(head4 "Shared (USA).gba" "$(devof4 b)" | cut -d' ' -f4)" = True ] || bad "slots: B's download not told to RomM: $(head4 "Shared (USA).gba" "$(devof4 b)")"
# the same on both sides: nothing sent, nothing taken
hs a; grep -q " 0 saves up, 0 down, 0 conflicts, 0 errors" "$OUT/hs-a.log" || bad "slots: an agreed save moved: $(cat "$OUT/hs-a.log")"
# changed on B only: sent; A, unchanged since, takes it
head -c 8192 /dev/urandom > "$GB/Shared (USA).srm"; hs b
grep -q " 1 saves up" "$OUT/hs-b.log" || bad "slots: B's change not sent: $(cat "$OUT/hs-b.log")"
hs a; cmp -s "$GA/Shared (USA).srm" "$GB/Shared (USA).srm" || bad "slots: A didn't get B's change: $(cat "$OUT/hs-a.log")"
# changed on both: held, both files as they were, the server's in A's vault, counted, and not fetched again the next
# time; once both hold the same bytes, agreed again
head -c 8192 /dev/urandom > "$GA/Shared (USA).srm"; cp "$GA/Shared (USA).srm" "$OUT/sa.srm"
head -c 8192 /dev/urandom > "$GB/Shared (USA).srm"; cp "$GB/Shared (USA).srm" "$OUT/sb.srm"
hs b; hs a
grep -q " 1 conflicts" "$OUT/hs-a.log" && grep -q 'conflict, both kept: saves/gba/Shared (USA).srm (the server.s in the vault)' "$OUT/hs-a.log" ||
    bad "slots: a conflict not held: $(cat "$OUT/hs-a.log")"
cmp -s "$GA/Shared (USA).srm" "$OUT/sa.srm" && [ "$(head4 "Shared (USA).gba" | cut -d' ' -f1)" = "$(m "$OUT/sb.srm")" ] ||
    bad "slots: a conflict changed a side: $(cat "$OUT/hs-a.log")"
ls "$OUT/h-a/banana/save-vault/saves/gba/Shared (USA)/" | grep -q -- "-$(m "$OUT/sb.srm" | cut -c1-8)\.srm$" || bad "slots: the server's side of a conflict isn't in the vault"
[ "$(rs a '[v["server_md5"] for v in c["conflicts"].values()]')" = "['$(m "$OUT/sb.srm")']" ] || bad "slots: the conflict isn't recorded: $(rs a 'c["conflicts"]')"
n=$(logc "GET /api/saves/$(head4 "Shared (USA).gba" | cut -d' ' -f3)/content"); hs a
grep -q " 1 conflicts" "$OUT/hs-a.log" && [ "$(logc "GET /api/saves/$(head4 "Shared (USA).gba" | cut -d' ' -f3)/content")" = "$n" ] ||
    bad "slots: a held conflict lost, or its server copy fetched again: $(cat "$OUT/hs-a.log")"
cp "$OUT/sb.srm" "$GA/Shared (USA).srm"; hs a
grep -q " 0 conflicts" "$OUT/hs-a.log" && [ "$(rs a 'len(c["conflicts"])')" = 0 ] || bad "slots: a conflict settled by hand stayed: $(cat "$OUT/hs-a.log")"
# a device deleted in RomM: registered again by the sync after a game, and the new one kept, so the next run doesn't
# find it gone; save sync then switched off for it in RomM: said, nothing sent, nothing left waiting as if the server
# were out of reach
hand d; GD=$OUT/h-d/saves/gba; printf 'homesync_auto = quit\n' >> "$OUT/h-d/banana/settings.conf"
hq() { "$BIN" --root "$OUT/h-$1" --run "$OUT/h-$1/run" --status "$OUT/h-$1/run/sync.status" sync --auto quit --rom "$2" > "$OUT/hq-$1.log" 2>&1; }
hs d; D1=$(devof4 d)
curl -s -o /dev/null -XDELETE -H "Authorization: Bearer $TA4" "$U4/api/devices/$D1"
head -c 8192 /dev/urandom > "$GD/Device (USA).srm"; hq d "gba/Device (USA).gba"
D2=$(devof4 d)
[ -n "$D2" ] && [ "$D2" != "$D1" ] && grep -q 'registered as RomM device' "$OUT/hq-d.log" || bad "slots: a deleted device not registered again and kept: $D1 $D2: $(cat "$OUT/hq-d.log")"
head -c 8192 /dev/urandom > "$GD/Device (USA).srm"; hq d "gba/Device (USA).gba"
grep -q 'the device was gone' "$OUT/hq-d.log" && bad "slots: the device registered again at every run: $(cat "$OUT/hq-d.log")"
curl -s -o /dev/null -XPUT -H "Authorization: Bearer $TA4" -H 'Content-Type: application/json' -d '{"sync_enabled": false}' "$U4/api/devices/$D2"
head -c 8192 /dev/urandom > "$GD/Device (USA).srm"; n=$(nv4 "Device (USA).gba"); hq d "gba/Device (USA).gba"
grep -q 'save sync is off for this handheld' "$OUT/hq-d.log" && [ "$(nv4 "Device (USA).gba")" = "$n" ] && [ "$(rs d 'len(c["queue"])')" = 0 ] ||
    bad "slots: save sync off for the device: $(rs d 'c["queue"]'): $(cat "$OUT/hq-d.log")"
curl -s -o /dev/null -XPUT -H "Authorization: Bearer $TA4" -H 'Content-Type: application/json' -d '{"sync_enabled": true}' "$U4/api/devices/$D2"
# settled on the Saves page. rv: the command, its one line
rv() { _n=$1; shift; "$BIN" --root "$OUT/h-$_n" --run "$OUT/h-$_n/run" "$@" 2>/dev/null | tail -1; }
SKEY="$(rid4 "Shared (USA).gba")/autosave"
hold2() {     # a conflict on A, its side $OUT/sa.srm, the server's (B's) $OUT/sb.srm
    hs a; hs b
    head -c 8192 /dev/urandom > "$GA/Shared (USA).srm"; cp "$GA/Shared (USA).srm" "$OUT/sa.srm"
    head -c 8192 /dev/urandom > "$GB/Shared (USA).srm"; cp "$GB/Shared (USA).srm" "$OUT/sb.srm"
    hs b; hs a
    [ "$(rs a '"'"$SKEY"'" in c["conflicts"]')" = True ] || bad "slots: no conflict to settle: $(cat "$OUT/hs-a.log")"
}
# (the listing: both sides, nothing asked of the server)
apic() { echo $(( $(logc "GET /api/") + $(logc "POST /api/") )); }
hold2; n=$(apic); "$BIN" --root "$OUT/h-a" --run "$OUT/h-a/run" saves > "$OUT/list" 2>&1
grep -q "^conflict	$SKEY	saves/gba/Shared (USA).srm		$(m "$OUT/sa.srm")	8192	$(m "$OUT/sb.srm")	8192	.*	both	gba/Shared (USA).gba	both$" "$OUT/list" && grep -q "^kept	saves/gba/Shared (USA).srm	" "$OUT/list" ||
    bad "slots: the Saves page's list: $(cat "$OUT/list")"
[ "$(apic)" = "$n" ] || bad "slots: the Saves page's list asked the server"
# (the server's side named by the device it came from, as its sync record has it: B's name, not its id)
BN=$(s4 "/api/devices/$(devof4 b)" | jget '["name"]')
[ -n "$BN" ] && [ "$(grep "^conflict	$SKEY	" "$OUT/list" | cut -f9)" = "$BN" ] ||
    bad "slots: the conflict's other side not named by B's device name ($BN): $(grep "^conflict	$SKEY	" "$OUT/list" | cut -f9)"
"$BIN" --root "$OUT/h-a" --run "$OUT/h-a/run" saves --rom "gba/Other Game.gba" | grep -q "^conflict" && bad "slots: --rom listed another game's conflict"
# (the card's: sent, no overwrite, the server's stays in the vault, and the other handheld takes it)
[ "$(rv a resolve "$SKEY" card)" = ok ] && [ "$(head4 "Shared (USA).gba" | cut -d' ' -f1)" = "$(m "$OUT/sa.srm")" ] && [ "$(rs a '"'"$SKEY"'" in c["conflicts"]')" = False ] ||
    bad "slots: keeping this handheld's: $(head4 "Shared (USA).gba") $(rs a 'c["conflicts"]')"
ls "$OUT/h-a/banana/save-vault/saves/gba/Shared (USA)/" | grep -q -- "-$(m "$OUT/sb.srm" | cut -c1-8)\.srm$" || bad "slots: keeping this handheld's lost the server's copy"
cp "$OUT/sa.srm" "$OUT/sa-kept.srm"; hs b; cmp -s "$GB/Shared (USA).srm" "$OUT/sa-kept.srm" && hs a && grep -q " 0 conflicts" "$OUT/hs-a.log" || bad "slots: a kept save didn't settle on both: $(cat "$OUT/hs-b.log")"
# (the server's, with no server at all: placed from the vault, the card's kept, agreed by the next sync with no transfer)
hold2; SURL=$(rs a 'c["server"]'); sed -i "s#$SURL#http://127.0.0.1:9#" "$OUT/h-a/banana/romsync.json"
# (the card's, with no server: said as that, not as the last sync's words, and still held)
r=$(rv a resolve "$SKEY" card)
case $r in "error the server can't be reached: "?*) ;; *) bad "slots: keeping this handheld's with no server answered: $r" ;; esac
[ "$(rs a '"'"$SKEY"'" in c["conflicts"]')" = True ] || bad "slots: keeping this handheld's with no server let the conflict go"
[ "$(rv a resolve "$SKEY" server)" = ok ] && cmp -s "$GA/Shared (USA).srm" "$OUT/sb.srm" && [ "$(rs a '"'"$SKEY"'" in c["conflicts"]')" = False ] || bad "slots: using the server's: $(rs a 'c["conflicts"]')"
ls "$OUT/h-a/banana/save-vault/saves/gba/Shared (USA)/" | grep -q -- "-$(m "$OUT/sa.srm" | cut -c1-8)\.srm$" || bad "slots: using the server's lost the card's"
sed -i "s#http://127.0.0.1:9#$SURL#" "$OUT/h-a/banana/romsync.json"
n=$(logc "POST /api/saves?"); hs a; grep -q " 0 conflicts" "$OUT/hs-a.log" && grep -q " 0 saves up" "$OUT/hs-a.log" && [ "$(logc "POST /api/saves?")" = "$n" ] && [ "$(rs a 'c["slots"]["'"$SKEY"'"]["md5"]')" = "$(m "$OUT/sb.srm")" ] ||
    bad "slots: the server's save not agreed: $(cat "$OUT/hs-a.log")"
# (a save played since the choice was shown: never replaced. The server's moved since: held again, with what it now is)
hold2; head -c 8192 /dev/urandom > "$GA/Shared (USA).srm"; cp "$GA/Shared (USA).srm" "$OUT/sa2.srm"
case $(rv a resolve "$SKEY" server) in "error "*) cmp -s "$GA/Shared (USA).srm" "$OUT/sa2.srm" || bad "slots: a save played since replaced" ;; *) bad "slots: a save played since wasn't refused" ;; esac
hs a; hold2; head -c 8192 /dev/urandom > "$GB/Shared (USA).srm"; cp "$GB/Shared (USA).srm" "$OUT/sb2.srm"; hs b
case $(rv a resolve "$SKEY" card) in "error "*) ;; *) bad "slots: a server moved since the choice: not refused" ;; esac
[ "$(head4 "Shared (USA).gba" | cut -d' ' -f1)" = "$(m "$OUT/sb2.srm")" ] && cmp -s "$GA/Shared (USA).srm" "$OUT/sa.srm" && [ "$(rs a 'c["conflicts"]["'"$SKEY"'"]["server_md5"]')" = "$(m "$OUT/sb2.srm")" ] ||
    bad "slots: a server moved since the choice: not held anew: $(rs a 'c["conflicts"]')"
# (a game running: nothing touched)
sleep 30 & GP=$!; echo $GP > "$OUT/h-a/run/game.pid"
[ "$(rv a resolve "$SKEY" server)" = "error a game is running" ] && cmp -s "$GA/Shared (USA).srm" "$OUT/sa.srm" || bad "slots: settled under a running game"
[ "$(rv a restore "$(rs a 'c["conflicts"]["'"$SKEY"'"]["vault"]')")" = "error a game is running" ] || bad "slots: restored under a running game"
rv a resolve "$SKEY" card | grep -q "^error" || bad "slots: kept under a running game"; cmp -s "$GA/Shared (USA).srm" "$OUT/sa.srm" || bad "slots: a keep touched the card under a game"
kill $GP; wait $GP 2>/dev/null; rm "$OUT/h-a/run/game.pid"
# (a kept copy restored: it goes up as any change, the card's kept; nothing outside the vault restored)
: > "$OUT/outside"; [ "$(rv a restore "$OUT/outside")" = "error not a kept copy in the vault" ] || bad "slots: a file outside the vault restored"
[ "$(rv a resolve "$SKEY" server)" = ok ] && hs a
KV=$(ls "$OUT/h-a/banana/save-vault/saves/gba/Shared (USA)/"*-*.srm | head -1); cp "$KV" "$OUT/kv.srm"; cp "$GA/Shared (USA).srm" "$OUT/before-r.srm"
[ "$(rv a restore "$KV")" = ok ] && cmp -s "$GA/Shared (USA).srm" "$OUT/kv.srm" || bad "slots: a kept copy not restored"
hs a; [ "$(head4 "Shared (USA).gba" | cut -d' ' -f1)" = "$(m "$OUT/kv.srm")" ] || bad "slots: a restored copy not sent: $(cat "$OUT/hs-a.log")"
ls "$OUT/h-a/banana/save-vault/saves/gba/Shared (USA)/" | grep -q -- "-$(m "$OUT/before-r.srm" | cut -c1-8)\.srm$" || bad "slots: restoring lost the card's save"
# (a blanked save brought back from the server's copy, which the sync kept for it)
cp "$GA/Shared (USA).srm" "$OUT/sh.srm"; head -c 8192 /dev/zero > "$GA/Shared (USA).srm"; hs a
"$BIN" --root "$OUT/h-a" --run "$OUT/h-a/run" saves | grep "^held	$SKEY" | cut -f12 | grep -qx server || bad "slots: a blanked save offers no way back: $("$BIN" --root "$OUT/h-a" --run "$OUT/h-a/run" saves | grep "^held")"
[ "$(rv a resolve "$SKEY" server)" = ok ] && cmp -s "$GA/Shared (USA).srm" "$OUT/sh.srm" || bad "slots: a blanked save not brought back"
[ "$(rv a resolve "$SKEY" card)" = "error nothing held for it" ] || bad "slots: settled twice"
# (restored with no save on the card; a state restored; status counts)
hs a; KV2=$(ls "$OUT/h-a/banana/save-vault/saves/gba/Shared (USA)/"*-*.srm | head -1); rm "$GA/Shared (USA).srm"; cp "$KV2" "$OUT/kv2.srm"
[ "$(rv a restore "$KV2")" = ok ] && cmp -s "$GA/Shared (USA).srm" "$OUT/kv2.srm" || bad "slots: restored onto no save"
hs a; [ "$(head4 "Shared (USA).gba" | cut -d' ' -f1)" = "$(m "$OUT/kv2.srm")" ] || bad "slots: a copy restored onto no save not sent: $(cat "$OUT/hs-a.log")"
"$BIN" --root "$OUT/h-a" --run "$OUT/h-a/run" status | grep -q "conflicts, .* held, .* waiting to be sent; vault .*: [1-9][0-9]* bytes" || bad "slots: status's second line"
# (another handheld's save arriving during the keep: RomM's 409, held again as what the server now has)
hs b; hold2; head -c 8192 /dev/urandom > "$GB/Shared (USA).srm"; cp "$GB/Shared (USA).srm" "$OUT/sb3.srm"
curl -s -XPOST "$U4/test/fault" -d '{"path": "POST /api/saves/[0-9]+/downloaded", "kind": "delay", "arg": 2}' >/dev/null
rv a resolve "$SKEY" card > "$OUT/keep409" & KP=$!; sleep 1; hs b; wait $KP
grep -q "^error" "$OUT/keep409" && cmp -s "$GA/Shared (USA).srm" "$OUT/sa.srm" && [ "$(head4 "Shared (USA).gba" | cut -d' ' -f1)" = "$(m "$OUT/sb3.srm")" ] && [ "$(rs a 'c["conflicts"]["'"$SKEY"'"]["server_md5"]')" = "$(m "$OUT/sb3.srm")" ] ||
    bad "slots: a 409 during a keep: $(cat "$OUT/keep409") $(rs a 'c["conflicts"]')"
rv a resolve "$SKEY" card > /dev/null; hs b
# (no server to keep it to: said, and still held. A conflict for two games of a name: each its own)
hs b; hold2; SURL=$(rs a 'c["server"]'); sed -i "s#$SURL#http://127.0.0.1:9#" "$OUT/h-a/banana/romsync.json"
rv a resolve "$SKEY" card | grep -q "^error" && cmp -s "$GA/Shared (USA).srm" "$OUT/sa.srm" || bad "slots: a keep with no server said ok"
sed -i "s#http://127.0.0.1:9#$SURL#" "$OUT/h-a/banana/romsync.json"; hs a
[ "$(rs a '"'"$SKEY"'" in c["conflicts"]')" = True ] || bad "slots: a keep with no server lost the conflict: $(cat "$OUT/hs-a.log")"
cp "$OUT/h-a/banana/romsync.json" "$OUT/rs.keep"
python3 -c 'import json,sys; p=sys.argv[1]; c=json.load(open(p)); n=dict(c["conflicts"]["'"$SKEY"'"]); n["rom"]="nes/Shared (USA).nes"; c["conflicts"]["9/autosave"]=n; c["conflicts"]["'"$SKEY"'"]["rom"]="gba/Shared (USA).gba"; json.dump(c,open(p,"w"))' "$OUT/h-a/banana/romsync.json"
[ "$("$BIN" --root "$OUT/h-a" --run "$OUT/h-a/run" saves --rom "gba/Shared (USA).gba" | grep "^conflict" | cut -f2)" = "$SKEY" ] || bad "slots: --rom gave another system's game of the same name"
cp "$OUT/rs.keep" "$OUT/h-a/banana/romsync.json"
# (the game gone from the card: the run drops the note and sends nothing, which isn't settled)
mv "$OUT/h-a/roms/gba/Shared (USA).gba" "$OUT/shared.gba"; rv a resolve "$SKEY" card | grep -q "^error" || bad "slots: a keep that sent nothing said ok"
mv "$OUT/shared.gba" "$OUT/h-a/roms/gba/Shared (USA).gba"; hs a; rv a resolve "$SKEY" card > /dev/null
hs b; cp "$GA/Shared (USA).srm" "$OUT/sb.srm"     # (what the next cases take for the server's)
# a handheld new to the slot with a save of its own, other bytes than the head and a day newer by its clock: held,
# never newest-wins, nothing sent
hand c; head -c 8192 /dev/urandom > "$OUT/h-c/saves/gba/Shared (USA).srm"; cp "$OUT/h-c/saves/gba/Shared (USA).srm" "$OUT/sc.srm"
touch -d "@$(( $(date +%s) + 86400 ))" "$OUT/h-c/saves/gba/Shared (USA).srm"; hs c
grep -q " 1 conflicts" "$OUT/hs-c.log" && cmp -s "$OUT/h-c/saves/gba/Shared (USA).srm" "$OUT/sc.srm" && [ "$(head4 "Shared (USA).gba" | cut -d' ' -f1)" = "$(m "$OUT/sb.srm")" ] ||
    bad "slots: a new handheld's own save: $(cat "$OUT/hs-c.log")"
# deleted on A after it synced: nothing deleted on the server, and never refilled, even when the server moves on
rm "$GA/Shared (USA).srm"; hs a
[ "$(head4 "Shared (USA).gba" | cut -d' ' -f1)" = "$(m "$OUT/sb.srm")" ] || bad "slots: a save deleted on the card changed the server's"
grep -q 'missing here since the last sync, left as it is: saves/gba/Shared (USA).srm' "$OUT/hs-a.log" || bad "slots: a deleted save not said: $(cat "$OUT/hs-a.log")"
head -c 8192 /dev/urandom > "$GB/Shared (USA).srm"; hs b; hs a
[ -e "$GA/Shared (USA).srm" ] && bad "slots: a save deleted on the card was refilled from the server"
# deleted on the server (RomM's web UI): not sent back until it changes here
head -c 8192 /dev/urandom > "$GA/Gone (USA).srm"; hs a
curl -s -XPOST -H "Authorization: Bearer $TA4" -H 'Content-Type: application/json' -d "{\"saves\": [$(head4 "Gone (USA).gba" | cut -d' ' -f3)]}" "$U4/api/saves/delete" >/dev/null
hs a; [ "$(nv4 "Gone (USA).gba")" = 0 ] || bad "slots: a save deleted on the server was sent back: $(cat "$OUT/hs-a.log")"
head -c 8192 /dev/urandom > "$GA/Gone (USA).srm"; hs a; [ "$(nv4 "Gone (USA).gba")" = 1 ] || bad "slots: a change after a server delete wasn't sent: $(cat "$OUT/hs-a.log")"
# a save of a size its system's saves don't come in (one cut short as the emulator wrote it): not sent, said
head -c 8197 /dev/urandom > "$GA/Gone (USA).srm"; hs a
[ "$(nv4 "Gone (USA).gba")" = 1 ] && grep -q "error: saves/gba/Gone (USA).srm: not a size this system's saves come in, not sent" "$OUT/hs-a.log" ||
    bad "slots: a save of no GBA size was sent, or not said: $(cat "$OUT/hs-a.log")"
head -c 8192 /dev/urandom > "$GA/Gone (USA).srm"; hs a
# a head that is a fresh core's blank save (another handheld's, never played) is no save: A's goes over it, RomM told
# first that A has seen it (its guard refuses an upload over a head the device hasn't seen)
mkdir -p "$OUT/peer4"; TP=$(curl -s -XPOST "$U4/test/token" -d '{}' | jget '["token"]')
blank "$OUT/bh.srm" 131072 ff
[ "$(python3 "$HERE/tests/romm-peers.py" "$U4" "$TP" bananaos "$OUT/peer4" upload "$(rid4 "Blank Head (USA).gba")" "$OUT/bh.srm")" = 200 ] || bad "slots: the peer's blank upload"
head -c 8192 /dev/urandom > "$GA/Blank Head (USA).srm"; hs a
[ "$(head4 "Blank Head (USA).gba" | cut -d' ' -f1)" = "$(m "$GA/Blank Head (USA).srm")" ] || bad "slots: a save over a blank head: $(cat "$OUT/hs-a.log")"
# a version comes in between the listing and A's upload (another handheld's): RomM refuses A's (409), the slot is
# looked at again and held, never sent over; the same race with the other handheld sending A's very bytes: agreed.
# race <ROM file> <A's save> <the other's file> [same|dsv]: the other's file as it is, or A's new save (as a .dsv)
race() {
    _r=$(rid4 "$1")
    head -c 8192 /dev/urandom > "$2"; hs a
    python3 "$HERE/tests/romm-peers.py" "$U4" "$TP" bananaos "$OUT/peer4" join "$_r" >/dev/null
    head -c 8192 /dev/urandom > "$2"
    case $4 in same) cp "$2" "$3" ;; dsv) dsv "$2" "$3" ;; esac
    curl -s -XPOST "$U4/test/fault" -d '{"path": "GET /api/saves$", "kind": "delay", "arg": 3}' >/dev/null
    "$BIN" --root "$OUT/h-a" --run "$OUT/h-a/run" --status "$OUT/h-a/run/sync.status" sync --saves-only > "$OUT/hs-a.log" 2>&1 & _k=$!
    sleep 1; python3 "$HERE/tests/romm-peers.py" "$U4" "$TP" bananaos "$OUT/peer4" upload "$_r" "$3" > "$OUT/race.code"; wait $_k
    [ "$(cat "$OUT/race.code")" = 200 ] || bad "slots: the racing upload: $(cat "$OUT/race.code")"
}
head -c 8192 /dev/urandom > "$OUT/peer.srm"; race "Race (USA).gba" "$GA/Race (USA).srm" "$OUT/peer.srm"
grep -q 'conflict, both kept: saves/gba/Race (USA).srm' "$OUT/hs-a.log" && [ "$(head4 "Race (USA).gba" | cut -d' ' -f1)" = "$(m "$OUT/peer.srm")" ] && [ "$(rs a 'len([k for k in c["conflicts"] if k.startswith("'"$(rid4 "Race (USA).gba")"'/")])')" = 1 ] ||
    bad "slots: a refused upload not held: $(cat "$OUT/hs-a.log")"
race "Race Same (USA).gba" "$GA/Race Same (USA).srm" "$OUT/peer.srm" same
grep -q "the server's newest is this same save: saves/gba/Race Same (USA).srm" "$OUT/hs-a.log" && ! grep -q 'conflict, both kept: saves/gba/Race Same' "$OUT/hs-a.log" &&
    [ "$(head4 "Race Same (USA).gba" "$(devof4 a)" | cut -d' ' -f1,4)" = "$(m "$GA/Race Same (USA).srm") True" ] || bad "slots: a refused upload of the same bytes: $(cat "$OUT/hs-a.log")"
# the other's the same save as DraStic's .dsv: agreed, the server's MD5 for it kept; one of a size this handheld's
# saves for the game don't have: held, kept in the vault, not a conflict
mkdir -p "$OUT/race"; DA=$OUT/h-a/saves/nds/"Race Dsv (USA).sav"
race "Race Dsv (USA).nds" "$DA" "$OUT/race/Race Dsv (USA).dsv" dsv
grep -q "the server's newest is this same save in another form: saves/nds/Race Dsv (USA).sav" "$OUT/hs-a.log" && ! grep -q 'conflict, both kept: saves/nds/Race Dsv' "$OUT/hs-a.log" &&
    [ "$(head4 "Race Dsv (USA).nds" "$(devof4 a)" | cut -d' ' -f1,4)" = "$(m "$OUT/race/Race Dsv (USA).dsv") True" ] &&
    [ "$(rs a 'c["slots"]["'"$(rid4 "Race Dsv (USA).nds")"'/autosave"].get("server_md5")')" = "$(m "$OUT/race/Race Dsv (USA).dsv")" ] ||
    bad "slots: a refused upload, the other's the same save as a .dsv: $(cat "$OUT/hs-a.log")"
head -c 32768 /dev/urandom > "$OUT/race/Race Size (USA).srm"; race "Race Size (USA).gba" "$GA/Race Size (USA).srm" "$OUT/race/Race Size (USA).srm"
grep -q "not taken yet, another size than this handheld's save for the game: Race Size (USA)" "$OUT/hs-a.log" && ! grep -q 'conflict, both kept: saves/gba/Race Size' "$OUT/hs-a.log" &&
    ! grep -q 'Race Size (USA).srm: changed on the server meanwhile' "$OUT/hs-a.log" &&
    [ "$(head4 "Race Size (USA).gba" | cut -d' ' -f1)" = "$(m "$OUT/race/Race Size (USA).srm")" ] &&
    [ "$(rs a 'sum(1 for v in c["held"].values() if v["path"] == "saves/gba/Race Size (USA).srm" and v["vault"])')" = 1 ] ||
    bad "slots: a refused upload, the other's of another size: not held: $(cat "$OUT/hs-a.log")"
# no answer from the server to an upload: waiting to be sent, its bytes in the vault; sent by the next sync
head -c 8192 /dev/urandom > "$GA/Queued (USA).srm"
curl -s -XPOST "$U4/test/fault" -d '{"path": "POST /api/saves$", "kind": "500"}' >/dev/null; hs a
qm=$(rs a '" ".join(v["md5"] for v in c["queue"].values())'); qv=$(rs a '" ".join(v["vault"] for v in c["queue"].values())')
[ "$qm" = "$(m "$GA/Queued (USA).srm")" ] && case $qv in */save-vault/saves/gba/"Queued (USA)"/*) cmp -s "$qv" "$GA/Queued (USA).srm" ;; *) false ;; esac &&
    grep -q '1 save waiting to be sent' "$OUT/hs-a.log" || bad "slots: an upload that failed isn't waiting: $qm $qv $(cat "$OUT/hs-a.log")"
hs a; [ "$(head4 "Queued (USA).gba" | cut -d' ' -f1)" = "$(m "$GA/Queued (USA).srm")" ] && [ "$(rs a 'len(c["queue"])')" = 0 ] ||
    bad "slots: a waiting save not sent: $(cat "$OUT/hs-a.log")"
# one gone from the card before it could be sent: no longer waiting
head -c 8192 /dev/urandom > "$GA/Queued Gone (USA).srm"
curl -s -XPOST "$U4/test/fault" -d '{"path": "POST /api/saves$", "kind": "500"}' >/dev/null; hs a
[ "$(rs a 'len(c["queue"])')" = 1 ] || bad "slots: a second failed upload isn't waiting: $(cat "$OUT/hs-a.log")"
rm "$GA/Queued Gone (USA).srm"; hs a
[ "$(rs a 'len(c["queue"])')" = 0 ] && ! grep -q 'waiting to be sent' "$OUT/hs-a.log" || bad "slots: a save gone from the card still waits: $(rs a 'c["queue"]')"
# a held conflict whose server side moves on while the new version can't be fetched: still held as it was, and
# recorded anew once it can be
RF="Refetch (USA)" RK="$(rid4 "Refetch (USA).gba")/autosave"
head -c 8192 /dev/urandom > "$GA/$RF.srm"; hs a; hs b
head -c 8192 /dev/urandom > "$GA/$RF.srm"; head -c 8192 /dev/urandom > "$GB/$RF.srm"; hs b; hs a
id0=$(head4 "$RF.gba" | cut -d' ' -f3)
python3 "$HERE/tests/romm-peers.py" "$U4" "$TP" bananaos "$OUT/peer4" join "$(rid4 "$RF.gba")" >/dev/null
head -c 8192 /dev/urandom > "$OUT/rf.srm"
[ "$(python3 "$HERE/tests/romm-peers.py" "$U4" "$TP" bananaos "$OUT/peer4" upload "$(rid4 "$RF.gba")" "$OUT/rf.srm")" = 200 ] || bad "slots: the peer's upload over a conflict"
id1=$(head4 "$RF.gba" | cut -d' ' -f3)
curl -s -XPOST "$U4/test/fault" -d "{\"path\": \"GET /api/saves/$id1/content\", \"kind\": \"500\"}" >/dev/null; hs a
[ "$(rs a 'c["conflicts"]["'"$RK"'"]["save_id"]')" = "$id0" ] || bad "slots: a conflict lost when the server's new side can't be fetched: $(rs a 'c["conflicts"]') $(cat "$OUT/hs-a.log")"
hs a; [ "$(rs a 'c["conflicts"]["'"$RK"'"]["save_id"]')" = "$id1" ] && grep -q 'conflict, both kept: saves/gba/Refetch (USA).srm' "$OUT/hs-a.log" ||
    bad "slots: a conflict not recorded anew: $(rs a 'c["conflicts"]') $(cat "$OUT/hs-a.log")"
# while the card's copy of that game isn't the server's ROM, the conflict stays as it was; once the game has no save
# in play on either side (deleted on the card and on the server), its records go
RR=$OUT/h-a/roms/gba/$RF.gba; cp "$RR" "$OUT/rf.gba"; printf x >> "$RR"; hs a
grep -q "not the server's ROM (other bytes): .*$RF" "$OUT/hs-a.log" && [ "$(rs a '"'"$RK"'" in c["conflicts"]')" = True ] ||
    bad "slots: a conflict lost while its game can't be said for sure: $(rs a 'c["conflicts"]') $(cat "$OUT/hs-a.log")"
cp "$OUT/rf.gba" "$RR"; rm "$GA/$RF.srm"
curl -s -XPOST -H "Authorization: Bearer $TA4" -H 'Content-Type: application/json' \
    -d "{\"saves\": [$(s4 "/api/saves?rom_id=$(rid4 "$RF.gba")" | python3 -c 'import json,sys; print(", ".join(str(s["id"]) for s in json.load(sys.stdin)))')]}" "$U4/api/saves/delete" >/dev/null
hs a; [ "$(nv4 "$RF.gba")" = 0 ] && [ "$(rs a '"'"$RK"'" in c["conflicts"] or "'"$RK"'" in c["held"]')" = False ] ||
    bad "slots: the records of a game with no save in play stayed: $(rs a 'c["conflicts"], c["held"]') $(cat "$OUT/hs-a.log")"
# the card's file times a day behind, then a day ahead: still decided by content (RomM's own plan, which goes by the
# times, has three of these four wrong)
past() { touch -d "@$(( $(date +%s) - 86400 ))" "$1"; }
ahead() { touch -d "@$(( $(date +%s) + 86400 ))" "$1"; }
SA=$GA/"Skew (USA).srm" SB=$GB/"Skew (USA).srm"
head -c 8192 /dev/urandom > "$SA"; hs a; hs b
head -c 8192 /dev/urandom > "$SA"; past "$SA"; hs a
[ "$(head4 "Skew (USA).gba" | cut -d' ' -f1)" = "$(m "$SA")" ] || bad "slots: behind, changed here only: not sent: $(cat "$OUT/hs-a.log")"
head -c 8192 /dev/urandom > "$SB"; past "$SB"; hs b
grep -q " 1 conflicts" "$OUT/hs-b.log" || bad "slots: behind, changed on both: not held: $(cat "$OUT/hs-b.log")"
python3 "$HERE/tests/romm-peers.py" "$U4" "$TP" bananaos "$OUT/peer4" join "$(rid4 "Skew (USA).gba")" >/dev/null
head -c 8192 /dev/urandom > "$OUT/sp.srm"
[ "$(python3 "$HERE/tests/romm-peers.py" "$U4" "$TP" bananaos "$OUT/peer4" upload "$(rid4 "Skew (USA).gba")" "$OUT/sp.srm")" = 200 ] || bad "slots: the peer's upload"
ahead "$SA"; hs a; cmp -s "$SA" "$OUT/sp.srm" || bad "slots: ahead, changed on the server only: not taken: $(cat "$OUT/hs-a.log")"
head -c 8192 /dev/urandom > "$SA"; ahead "$SA"; hs a
[ "$(head4 "Skew (USA).gba" | cut -d' ' -f1)" = "$(m "$SA")" ] || bad "slots: ahead, changed here only: not sent: $(cat "$OUT/hs-a.log")"
# a dry run: says what it would do, and sends nothing (not even a negotiation, which opens a session)
head -c 8192 /dev/urandom > "$GA/Dry (USA).srm"; cp "$OUT/h-a/banana/romsync.json" "$OUT/before.json"; n=$(logc POST)
hs a --dry-run
grep -q 'upload saves/gba/Dry (USA).srm' "$OUT/hs-a.log" || bad "slots: a dry run didn't say the upload: $(cat "$OUT/hs-a.log")"
[ "$(logc POST)" = "$n" ] && cmp -s "$OUT/h-a/banana/romsync.json" "$OUT/before.json" || bad "slots: a dry run changed something: $(logc POST) POSTs (were $n)"
# a handheld that can't be a RomM device (a token without the devices scopes): the slots' saves come down, its own
# wait, said (RomM's guards are a device's)
head -c 8192 /dev/urandom > "$GA/Joiner (USA).srm"; hs a
hand e '{"scopes": ["me.read", "roms.read", "platforms.read", "assets.read", "assets.write"]}'
head -c 8192 /dev/urandom > "$OUT/h-e/saves/gba/Lone (USA).srm"; hs e
cmp -s "$OUT/h-e/saves/gba/Joiner (USA).srm" "$GA/Joiner (USA).srm" || bad "slots: no device: a save didn't come down: $(cat "$OUT/hs-e.log")"
[ "$(nv4 "Lone (USA).gba")" = 0 ] && grep -q "this handheld's saves are sent once it is a RomM device" "$OUT/hs-e.log" || bad "slots: no device: $(cat "$OUT/hs-e.log")"
# an answer from RomM that can't be what it says (a negotiation without its session, a listing with a game it wasn't
# asked about, or not JSON at all): the saves stop there, said, nothing sent or taken
head -c 8192 /dev/urandom > "$GA/Closed (USA).srm"; cp "$GA/Closed (USA).srm" "$OUT/closed.srm"
for f in 'POST /api/sync/negotiate|{"operations": []}|negotiation can.t be read' \
    'GET /api/saves$|[{"id": 1, "rom_id": 99999, "slot": "autosave", "file_name": "x.srm", "content_hash": "", "updated_at": ""}]|listed a save of a game it wasn.t asked about' \
    'GET /api/saves$|<html>busy</html>|saves can.t be read'; do
    path=${f%%|*} rest=${f#*|}; body=${rest%%|*} want=${rest#*|}
    curl -s -XPOST "$U4/test/fault" -d "$(python3 -c 'import json,sys; print(json.dumps({"path": sys.argv[1], "kind": "replace", "arg": sys.argv[2]}))' "$path" "$body")" >/dev/null
    n=$(logc "POST /api/saves?"); hs a
    grep -q "error: saves: .*$want" "$OUT/hs-a.log" && [ "$(logc "POST /api/saves?")" = "$n" ] && cmp -s "$GA/Closed (USA).srm" "$OUT/closed.srm" &&
        ! grep -q '^  up \|^  down ' "$OUT/hs-a.log" || bad "slots: $path answered with $body: $(cat "$OUT/hs-a.log")"
    # (the session that negotiation opened, which banana-sync never saw: left open, as RomM leaves one never completed)
    case $path in *negotiate) LOST=$(s4 "/api/sync/sessions?device_id=$(devof4 a)&limit=1" | jget '[0]["id"]') ;; esac
done
hs a; [ "$(head4 "Closed (USA).gba" | cut -d' ' -f1)" = "$(m "$OUT/closed.srm")" ] || bad "slots: the saves after RomM answered right again: $(cat "$OUT/hs-a.log")"
# a card that synced with banana-sync 1.0 (its saves unslotted, their MD5s in romsync.json's saves): a slot whose head
# is the very save the card and its old record hold is agreed, nothing sent or taken; one whose head is other bytes is
# a conflict, held, whatever the old record says; the unslotted rows left where they are
hand u; GU=$OUT/h-u/saves/gba
for g in Kept Diverged; do
    head -c 8192 /dev/urandom > "$GU/$g (USA).srm"
    curl -s -XPOST "$U4/test/save" -d "{\"rom_id\": $(rid4 "$g (USA).gba"), \"file_name\": \"$g (USA).srm\", \"emulator\": \"retroarch\", \"data\": \"$(od -An -v -tx1 "$GU/$g (USA).srm" | tr -d ' \n')\"}" >/dev/null
done
head -c 8192 /dev/urandom > "$OUT/div.srm"
for f in "Kept:$GU/Kept (USA).srm" "Diverged:$OUT/div.srm"; do
    curl -s -XPOST "$U4/test/save" -d "{\"rom_id\": $(rid4 "${f%%:*} (USA).gba"), \"slot\": \"autosave\", \"file_name\": \"${f%%:*} (USA).srm\", \"emulator\": \"mgba\", \"data\": \"$(od -An -v -tx1 "${f#*:}" | tr -d ' \n')\"}" >/dev/null
done
python3 -c '
import json, hashlib, sys
p, d = sys.argv[1], sys.argv[2]
c = json.load(open(p))
c["saves"] = {"saves/gba/%s (USA).srm" % g: {"version": 1, "md5": hashlib.md5(open("%s/%s (USA).srm" % (d, g), "rb").read()).hexdigest()} for g in ("Kept", "Diverged")}
json.dump(c, open(p, "w"))' "$OUT/h-u/banana/romsync.json" "$GU"
cp "$GU/Kept (USA).srm" "$OUT/kept.srm"; cp "$GU/Diverged (USA).srm" "$OUT/diverged.srm"; hs u
[ "$(rs u 'c["slots"]["'"$(rid4 "Kept (USA).gba")"'/autosave"]["md5"]')" = "$(m "$OUT/kept.srm")" ] && cmp -s "$GU/Kept (USA).srm" "$OUT/kept.srm" &&
    grep -q " 0 saves up, .* 1 conflicts" "$OUT/hs-u.log" && ! grep -q 'Kept (USA)' "$OUT/hs-u.log" ||
    bad "slots: a 1.0 card's agreed save: $(cat "$OUT/hs-u.log")"
cmp -s "$GU/Diverged (USA).srm" "$OUT/diverged.srm" && [ "$(head4 "Diverged (USA).gba" | cut -d' ' -f1)" = "$(m "$OUT/div.srm")" ] &&
    grep -q 'conflict, both kept: saves/gba/Diverged (USA).srm' "$OUT/hs-u.log" || bad "slots: a 1.0 card's save against other bytes: $(cat "$OUT/hs-u.log")"
[ "$(nv4 "Kept (USA).gba")" = 2 ] && [ "$(nv4 "Diverged (USA).gba")" = 2 ] || bad "slots: the unslotted rows: $(nv4 "Kept (USA).gba") $(nv4 "Diverged (USA).gba")"
# one game's save (sync --rom, its ROM's path roms-relative or absolute): only that game looked at and only its
# system's games listed; another game's changed save waits for a full sync, and every other game's records stay
hr() { _n=$1; shift; "$BIN" --root "$OUT/h-$_n" --run "$OUT/h-$_n/run" --status "$OUT/h-$_n/run/sync.status" sync "$@" > "$OUT/hr-$_n.log" 2>&1; }
others() { rs a 'json.dumps({k: [c[m].get(k) for m in ("conflicts", "held", "queue")] for k in set(c["conflicts"]) | set(c["held"]) | set(c["queue"]) if k != "'"$(rid4 "Solo (USA).gba")"'/autosave"}, sort_keys=True)'; }
NDSP=$(s4 /api/platforms | python3 -c 'import json,sys; print(next(p["id"] for p in json.load(sys.stdin) if p["fs_slug"] == "nds"))')
head -c 8192 /dev/urandom > "$GA/Solo (USA).srm"; head -c 8192 /dev/urandom > "$GA/Other (USA).srm"
# (banana-sync's listing, which names the platform twice; the test's own lookups name it once)
o=$(others); n=$(logc "GET /api/roms?platform_ids=$NDSP&platform_id=")
[ "$(rs a 'len(c["held"]) + len(c["conflicts"])')" -gt 0 ] || bad "slots: --rom: no other game's records to keep"
hr a --rom "$OUT/h-a/roms/gba/Solo (USA).gba"
[ "$(head4 "Solo (USA).gba" | cut -d' ' -f1)" = "$(m "$GA/Solo (USA).srm")" ] && [ "$(nv4 "Other (USA).gba")" = 0 ] && [ "$(others)" = "$o" ] &&
    [ "$(logc "GET /api/roms?platform_ids=$NDSP&platform_id=")" = "$n" ] || bad "slots: --rom: not that game's save alone: $(others) (was $o) $(cat "$OUT/hr-a.log")"
# (its own status line, not the Home server page's, whose Stop is for a sync copying games)
echo untouched > "$OUT/h-a/run/sync.status"; hr a --rom "gba/Solo (USA).gba"
grep -q '^done ' "$OUT/h-a/run/gameend.status" && [ "$(cat "$OUT/h-a/run/sync.status")" = untouched ] && [ ! -e "$OUT/h-a/run/sync.pid" ] ||
    bad "slots: --rom on its own: the status line: $(cat "$OUT/h-a/run/gameend.status") / $(cat "$OUT/h-a/run/sync.status")"
hr a --rom "gba/Nowhere (USA).gba" && grep -q 'not a game on the server: gba/Nowhere (USA).gba' "$OUT/hr-a.log" || bad "slots: --rom of no game: $(cat "$OUT/hr-a.log")"
hs a; [ "$(nv4 "Other (USA).gba")" = 1 ] || bad "slots: the other game's save, the next full sync: $(cat "$OUT/hs-a.log")"
# beside a sync copying games (sync.lock held), one game's save goes at once, and neither run writes over what the
# other wrote in romsync.json: the games the first has copied so far, the save the second has agreed (a game started
# meanwhile, so the first leaves the saves alone and writes only its games; the two games asked for in Get games)
printf 'gba rom conc a' > "$L4/gba/Conc A (USA).gba"; printf 'gba rom conc b' > "$L4/gba/Conc B (USA).gba"
"$BIN" --root "$OUT/h-a" --run "$OUT/h-a/run" get "gba/Conc A (USA).gba" "gba/Conc B (USA).gba" >/dev/null
curl -s -XPOST "$U4/test/fault" -d '{"path": "GET /api/roms/.*/content/Conc(%20| )B", "kind": "delay", "arg": 6}' >/dev/null
hr a & _k=$!
for i in $(seq 50); do [ "$(rs a '"gba/Conc A (USA).gba" in c["state"]["files"]' 2>/dev/null)" = True ] && break; sleep 0.1; done
head -c 8192 /dev/urandom > "$GA/Solo (USA).srm"; t=$(date +%s)
"$BIN" --root "$OUT/h-a" --run "$OUT/h-a/run" --status "$OUT/h-a/run/sync.status" sync --rom "gba/Solo (USA).gba" > "$OUT/hr-a2.log" 2>&1
[ $(( $(date +%s) - t )) -lt 4 ] && [ "$(head4 "Solo (USA).gba" | cut -d' ' -f1)" = "$(m "$GA/Solo (USA).srm")" ] &&
    [ "$(rs a '"gba/Conc A (USA).gba" in c["state"]["files"]')" = True ] || bad "slots: --rom beside a sync copying games: $(cat "$OUT/hr-a2.log")"
grep -q '^running ' "$OUT/h-a/run/sync.status" || bad "slots: --rom took the status line of the sync copying games: $(cat "$OUT/h-a/run/sync.status")"
sleep 30 & _g=$!; echo "$_g" > "$OUT/h-a/run/game.pid"
wait $_k; kill "$_g"; rm "$OUT/h-a/run/game.pid"
grep -q 'saves: a game is running' "$OUT/hr-a.log" || bad "slots: the sync copying games took the saves with a game running: $(cat "$OUT/hr-a.log")"
[ "$(rs a 'c["slots"]["'"$(rid4 "Solo (USA).gba")"'/autosave"]["md5"]')" = "$(m "$GA/Solo (USA).srm")" ] && [ "$(rs a '"gba/Conc B (USA).gba" in c["state"]["files"]')" = True ] ||
    bad "slots: the two runs' romsync.json: $(rs a 'c["slots"].get("'"$(rid4 "Solo (USA).gba")"'/autosave"), sorted(c["state"]["files"])') $(cat "$OUT/hr-a.log")"
# a game started while the sync copying games waited for the saves (another sync had them): it has the lock with the
# game already there, leaves the saves alone and lets the lock go at once, not at its end, after the games asked for
# meanwhile: the sync after that game, which waits 10 s for the lock, has it while the first still copies
printf 'gba rom conc c' > "$L4/gba/Conc C (USA).gba"
python3 -c 'import fcntl, sys, time; f = open(sys.argv[1], "a"); fcntl.flock(f, fcntl.LOCK_EX); time.sleep(60)' "$OUT/h-a/run/save.lock" & _h=$!
for i in $(seq 50); do python3 -c 'import fcntl, sys; fcntl.flock(open(sys.argv[1], "a"), fcntl.LOCK_EX | fcntl.LOCK_NB)' "$OUT/h-a/run/save.lock" 2>/dev/null || break; sleep 0.1; done
hr a & _k=$!
for i in $(seq 100); do grep -q "saves: waiting for another sync" "$OUT/hr-a.log" 2>/dev/null && break; sleep 0.1; done
grep -q "saves: waiting for another sync" "$OUT/hr-a.log" || bad "slots: the sync copying games didn't wait for the saves' lock: $(cat "$OUT/hr-a.log")"
sleep 30 & _g=$!; echo "$_g" > "$OUT/h-a/run/game.pid"
"$BIN" --root "$OUT/h-a" --run "$OUT/h-a/run" get "gba/Conc C (USA).gba" >/dev/null
curl -s -XPOST "$U4/test/fault" -d '{"path": "GET /api/roms/.*/content/Conc(%20| )C", "kind": "delay", "arg": 6}' >/dev/null
kill "$_h"; wait "$_h" 2>/dev/null
sleep 1.5
kill -0 "$_k" 2>/dev/null || bad "slots: the sync copying games ended before the lock could be looked at: $(cat "$OUT/hr-a.log")"
python3 -c 'import fcntl, sys; fcntl.flock(open(sys.argv[1], "a"), fcntl.LOCK_EX | fcntl.LOCK_NB)' "$OUT/h-a/run/save.lock" 2>/dev/null ||
    bad "slots: the saves' lock kept through the games asked for meanwhile, with a game running: $(cat "$OUT/hr-a.log")"
wait "$_k"; kill "$_g"; rm "$OUT/h-a/run/game.pid"
grep -q 'saves: a game is running' "$OUT/hr-a.log" && [ "$(rs a '"gba/Conc C (USA).gba" in c["state"]["files"]')" = True ] ||
    bad "slots: a game started during the wait for the saves' lock: $(cat "$OUT/hr-a.log")"
# one game's run stopped (as when the time before a game runs out): the status line says so, and another sync's
# sync.pid, for the menu's Stop, stays
echo 4242 > "$OUT/h-a/run/sync.pid"
# (stopped before its negotiation, so no session is left open)
curl -s -XPOST "$U4/test/fault" -d '{"path": "GET /api/platforms$", "kind": "delay", "arg": 5}' >/dev/null
"$BIN" --root "$OUT/h-a" --run "$OUT/h-a/run" --status "$OUT/h-a/run/sync.status" sync --rom "gba/Solo (USA).gba" > "$OUT/hr-a3.log" 2>&1 & _k=$!
sleep 1.5; kill -TERM "$_k"; wait "$_k"
[ "$(cat "$OUT/h-a/run/gameend.status")" = "error stopped" ] && [ -f "$OUT/h-a/run/sync.pid" ] ||
    bad "slots: --rom stopped: $(cat "$OUT/h-a/run/gameend.status"), sync.pid $(ls "$OUT/h-a/run")"
rm "$OUT/h-a/run/sync.pid"
# the card forgotten while one game's run goes on: that run's saves aren't written back as a pairing
cp "$OUT/h-a/banana/romsync.json" "$OUT/paired.json"
curl -s -XPOST "$U4/test/fault" -d '{"path": "GET /api/saves$", "kind": "delay", "arg": 3}' >/dev/null
head -c 8192 /dev/urandom > "$GA/Solo (USA).srm"; hr a --rom "gba/Solo (USA).gba" & _k=$!
sleep 1; "$BIN" --root "$OUT/h-a" --run "$OUT/h-a/run" --status "$OUT/h-a/run/sync.status" forget; wait "$_k"
[ -e "$OUT/h-a/banana/romsync.json" ] && bad "slots: --rom wrote a pairing back after a forget: $(cat "$OUT/h-a/banana/romsync.json")"
# (paired again as before: the save that run sent is the card's, agreed by the next)
cp "$OUT/paired.json" "$OUT/h-a/banana/romsync.json"; hr a --rom "gba/Solo (USA).gba"
# the same, paired again with the same server: a new token, and the run's saves aren't written over what it left
cp "$OUT/h-a/banana/romsync.json" "$OUT/paired.json"; s=$(rs a 'json.dumps(c["slots"], sort_keys=True)')
curl -s -XPOST "$U4/test/fault" -d '{"path": "GET /api/saves$", "kind": "delay", "arg": 3}' >/dev/null
head -c 8192 /dev/urandom > "$GA/Solo (USA).srm"; hr a --rom "gba/Solo (USA).gba" & _k=$!
sleep 1; sed -i 's/"token": "[^"]*"/"token": "another"/' "$OUT/h-a/banana/romsync.json"; wait "$_k"
[ "$(rs a 'json.dumps(c["slots"], sort_keys=True)')" = "$s" ] || bad "slots: --rom wrote its saves over a new pairing: $(cat "$OUT/hr-a.log")"
cp "$OUT/paired.json" "$OUT/h-a/banana/romsync.json"; hr a --rom "gba/Solo (USA).gba"
# a run of one game's says how it went, like any other
sed -i 's/"last": "[^"]*"/"last": "x"/' "$OUT/h-a/banana/romsync.json"; head -c 8192 /dev/urandom > "$GA/Solo (USA).srm"; hr a --rom "gba/Solo (USA).gba"
[ "$(rs a 'c["last"]')" = "ok 0 0 1 0 0" ] || bad "slots: --rom's last sync: $(rs a 'c["last"]') $(cat "$OUT/hr-a.log")"
# the saves one run at a time (save.lock): one game's waits for another run's to end, and gives up after 10 s
flock -x "$OUT/h-a/run/save.lock" sleep 2 & _k=$!; sleep 0.3
head -c 8192 /dev/urandom > "$GA/Solo (USA).srm"; hr a --rom "gba/Solo (USA).gba"; wait $_k
[ "$(head4 "Solo (USA).gba" | cut -d' ' -f1)" = "$(m "$GA/Solo (USA).srm")" ] || bad "slots: --rom after a wait for save.lock: $(cat "$OUT/hr-a.log")"
flock -x "$OUT/h-a/run/save.lock" sleep 12 & _k=$!; sleep 0.3
head -c 8192 /dev/urandom > "$GA/Solo (USA).srm"; hr a --rom "gba/Solo (USA).gba"; wait $_k
grep -q 'error: saves: another sync is saving, next time' "$OUT/hr-a.log" && [ "$(head4 "Solo (USA).gba" | cut -d' ' -f1)" != "$(m "$GA/Solo (USA).srm")" ] ||
    bad "slots: --rom with save.lock held throughout: $(cat "$OUT/hr-a.log")"
# a game started while one game's run waits for save.lock: that run doesn't go on to the saves
hr a --rom "gba/Solo (USA).gba"; s=$(head4 "Solo (USA).gba")
flock -x "$OUT/h-a/run/save.lock" sleep 2 & _f=$!; sleep 0.3
head -c 8192 /dev/urandom > "$GA/Solo (USA).srm"; hr a --rom "gba/Solo (USA).gba" & _k=$!
sleep 0.5; sleep 30 & _g=$!; echo $_g > "$OUT/h-a/run/game.pid"; wait $_f; wait $_k; kill $_g; rm "$OUT/h-a/run/game.pid"
grep -q 'error: saves: a game is running, next time' "$OUT/hr-a.log" && [ "$(head4 "Solo (USA).gba")" = "$s" ] ||
    bad "slots: --rom went on to the saves with a game started during its wait for save.lock: $(cat "$OUT/hr-a.log")"
# and one started during a download of one game's save: nothing goes under it, and the next sync takes it
hr a --rom "gba/Solo (USA).gba"; hs b; head -c 8192 /dev/urandom > "$GB/Solo (USA).srm"; hs b
w=$(m "$GB/Solo (USA).srm"); l=$(m "$GA/Solo (USA).srm"); s=$(rs a 'json.dumps(c["slots"], sort_keys=True)')
curl -s -XPOST "$U4/test/fault" -d '{"path": "GET /api/saves/[0-9]+/content", "kind": "delay", "arg": 2}' >/dev/null
hr a --rom "gba/Solo (USA).gba" & _k=$!
sleep 1; sleep 30 & _g=$!; echo $_g > "$OUT/h-a/run/game.pid"; wait $_k; kill $_g; rm "$OUT/h-a/run/game.pid"
grep -q 'a game started, next time' "$OUT/hr-a.log" && [ "$(m "$GA/Solo (USA).srm")" = "$l" ] && [ "$(rs a 'json.dumps(c["slots"], sort_keys=True)')" = "$s" ] ||
    bad "slots: a save placed under a game started during its download: $(cat "$OUT/hr-a.log")"
hr a --rom "gba/Solo (USA).gba"
[ "$(m "$GA/Solo (USA).srm")" = "$w" ] || bad "slots: the save held back from a game not taken after it: $(cat "$OUT/hr-a.log")"
# the sync before a game starts (--auto launch --rom): only with Auto sync on and Sync before playing on (off when not set), RomM's one
# game's; each request given 10 s at most, so a server that doesn't answer soon is left for the sync after the game
SA4=$OUT/h-a/banana/settings.conf
head -c 8192 /dev/urandom > "$GA/Solo (USA).srm"; n=$(logc "GET /api/platforms")
hr a --auto launch --rom "gba/Solo (USA).gba"
sed -i 's/^homesync_auto = off$/homesync_auto = quit/' "$SA4"; echo 'homesync_launch = off' >> "$SA4"
hr a --auto launch --rom "gba/Solo (USA).gba"
sed -i '/^homesync_launch/d' "$SA4"; hr a --auto launch --rom "gba/Solo (USA).gba"; echo 'homesync_launch = on' >> "$SA4"; hr a --auto launch
[ "$(logc "GET /api/platforms")" = "$n" ] && [ "$(head4 "Solo (USA).gba" | cut -d' ' -f1)" != "$(m "$GA/Solo (USA).srm")" ] ||
    bad "slots: a sync before playing with Auto sync off, Sync before playing off, or no game: $(cat "$OUT/hr-a.log")"
hr a --auto launch --rom "gba/Solo (USA).gba"
[ "$(head4 "Solo (USA).gba" | cut -d' ' -f1)" = "$(m "$GA/Solo (USA).srm")" ] || bad "slots: the sync before playing: $(cat "$OUT/hr-a.log")"
# (the saves held by another sync as it starts, the one after the last game still placing: said as waiting in its
# status, so the menu waits that out rather than counting it against the server; the run done once they are free)
head -c 8192 /dev/urandom > "$GA/Solo (USA).srm"; rm -f "$OUT/h-a/run/launch.status"
( flock -x 9; sleep 3 ) 9> "$OUT/h-a/run/save.lock" & _l=$!
sleep 0.3; hr a --auto launch --rom "gba/Solo (USA).gba" & _k=$!
sleep 1.5; [ "$(cat "$OUT/h-a/run/launch.status" 2>/dev/null)" = "waiting 0 0 |saves" ] || bad "slots: the wait for the saves before a game isn't said: $(cat "$OUT/h-a/run/launch.status")"
wait $_l; wait $_k
grep -q '^done ' "$OUT/h-a/run/launch.status" && [ "$(head4 "Solo (USA).gba" | cut -d' ' -f1)" = "$(m "$GA/Solo (USA).srm")" ] ||
    bad "slots: the sync before playing after a wait for the saves: $(cat "$OUT/h-a/run/launch.status") $(cat "$OUT/hr-a.log")"
curl -s -XPOST "$U4/test/fault" -d '{"path": "GET /api/saves$", "kind": "delay", "arg": 15}' >/dev/null
head -c 8192 /dev/urandom > "$GA/Solo (USA).srm"; t=$(date +%s); hr a --auto launch --rom "gba/Solo (USA).gba"
[ $(( $(date +%s) - t )) -lt 13 ] && grep -q 'error: saves: ' "$OUT/hr-a.log" && [ "$(head4 "Solo (USA).gba" | cut -d' ' -f1)" != "$(m "$GA/Solo (USA).srm")" ] ||
    bad "slots: the sync before playing, the server slow: $(( $(date +%s) - t )) s: $(cat "$OUT/hr-a.log")"
# (its own status file: the first answer said there, its end, and nothing on the menu's Sync row)
echo 'untouched' > "$OUT/h-a/run/sync.status"; rm -f "$OUT/h-a/run/launch.status"
curl -s -XPOST "$U4/test/fault" -d '{"path": "GET /api/saves$", "kind": "delay", "arg": 3}' >/dev/null
hr a --auto launch --rom "gba/Solo (USA).gba" & _k=$!
sleep 2; [ "$(cat "$OUT/h-a/run/launch.status" 2>/dev/null)" = answered ] || bad "slots: the first answer isn't said before the transfer: $(cat "$OUT/h-a/run/launch.status")"
wait $_k
grep -q '^done ' "$OUT/h-a/run/launch.status" && [ "$(cat "$OUT/h-a/run/sync.status")" = untouched ] ||
    bad "slots: the sync before playing's status: $(cat "$OUT/h-a/run/launch.status") / $(cat "$OUT/h-a/run/sync.status")"
# (what it holds for the game, for the menu's question: the Saves page's lines of it, on /run, none of the vault's)
hold2; hr a --auto launch --rom "gba/Shared (USA).gba"
grep -q "^conflict	$SKEY	saves/gba/Shared (USA).srm	" "$OUT/h-a/run/launch.conflict" && ! grep -q '^kept' "$OUT/h-a/run/launch.conflict" ||
    bad "slots: the sync before playing's conflict line: $(cat "$OUT/h-a/run/launch.conflict")"
[ "$(rv a resolve "$SKEY" card)" = ok ] || bad "slots: the conflict before playing not settled"
# (no answer to the first request: that's the run's end, not a second wait; the save stays for the sync after the game)
head -c 8192 /dev/urandom > "$GA/Solo (USA).srm"
curl -s -XPOST "$U4/test/fault" -d '{"path": "GET /api/heartbeat$", "kind": "delay", "arg": 15}' >/dev/null
hr a --auto launch --rom "gba/Solo (USA).gba"
grep -q 'error: saves: ' "$OUT/hr-a.log" && [ "$(head4 "Solo (USA).gba" | cut -d' ' -f1)" != "$(m "$GA/Solo (USA).srm")" ] ||
    bad "slots: the sync before playing, no answer to the heartbeat: $(cat "$OUT/hr-a.log")"
# a game outside both cards' ROMs folders (an app): nothing asked before it, no games listed after it
# and nothing said of it
n=$(logc "GET /api/heartbeat"); hr a --auto launch --rom "/elsewhere/Solo (USA).gba"
[ "$(logc "GET /api/heartbeat")" = "$n" ] || bad "slots: the sync before a game outside the ROMs folder asked the server: $(cat "$OUT/hr-a.log")"
n=$(logc "GET /api/platforms"); hr a --auto quit --rom "/elsewhere/Solo (USA).gba"
[ "$(logc "GET /api/platforms")" = "$n" ] && ! grep -q 'not a game' "$OUT/h-a/run/gameend.status" 2>/dev/null ||
    bad "slots: the sync after a game outside the ROMs folder: $(cat "$OUT/h-a/run/gameend.status") $(cat "$OUT/hr-a.log")"
# a newer save on the server that can't be fetched before a game: the card's left as it is, and the mark the menu asks by
hs a; hs b; cp "$GA/Solo (USA).srm" "$OUT/solo-keep.srm"
hs b; [ -f "$GB/Solo (USA).srm" ] || bad "slots: Solo not on B: $(cat "$OUT/hs-b.log")"
head -c 8192 /dev/urandom > "$GB/Solo (USA).srm"; hs b
curl -s -XPOST "$U4/test/fault" -d '{"path": "GET /api/saves/[0-9]+/content", "kind": "500"}' >/dev/null
hr a --auto launch --rom "gba/Solo (USA).gba"
[ -e "$OUT/h-a/run/launch.older" ] && cmp -s "$GA/Solo (USA).srm" "$OUT/solo-keep.srm" || bad "slots: a newer save not fetched before a game: no mark: $(cat "$OUT/hr-a.log")"
curl -s -XPOST "$U4/test/fault" -d '{"clear": true}' >/dev/null; rm -f "$OUT/h-a/run/launch.older"
hs a; cmp -s "$GA/Solo (USA).srm" "$GB/Solo (USA).srm" || bad "slots: Solo not back in step: $(cat "$OUT/hs-a.log")"
# a stop while the newer save is placed (the menu's B, or its cap, a SIGTERM banana-sync holds off while it places): the
# save in place, so the mark of a newer save not fetched goes with it; then the menu's last resort, the run killed with
# all it started, between the save put in place and its agreement recorded: nothing left running, and a game played
# over it before the next sync is a change from that version, sent, not a conflict with it. nds-slow: nds-saves, slow
# from its second run on (the first is the run's look at the game's two saves, before the download)
cat > "$OUT/nds-slow" <<EOF
#!/bin/sh
n=\$(( \$(cat "$OUT/nds-slow.n" 2>/dev/null || echo 0) + 1 )); echo \$n > "$OUT/nds-slow.n"
[ \$n -lt 2 ] || { touch "$OUT/nds-slow.in"; sleep 2; }
BANANA_ROOT=$OUT/h-a exec sh "$NDS" "\$@"
EOF
chmod +x "$OUT/nds-slow"
QA=$OUT/h-a/saves/nds/"Peel Quest (USA).sav"; QB=$OUT/h-b/saves/nds/"Peel Quest (USA).sav"
slowrun() {     # the sync before Peel Quest on A, in the background, its own group; $SR: its pid
    rm -f "$OUT/nds-slow.n" "$OUT/nds-slow.in" "$OUT/h-a/run/launch.older"
    BANANA_NDS_SAVES=$OUT/nds-slow setsid "$BIN" --root "$OUT/h-a" --run "$OUT/h-a/run" sync --auto launch --rom "nds/Peel Quest (USA).nds" > "$OUT/hr-a.log" 2>&1 & SR=$!
    for i in $(seq 100); do [ -e "$OUT/nds-slow.in" ] && break; sleep 0.1; done
    [ -e "$OUT/nds-slow.in" ] || bad "slots: the slow nds-saves never ran: $(cat "$OUT/hr-a.log")"
}
hs a; hs b; head -c 8192 /dev/urandom > "$QB"; hs b
slowrun; kill -TERM $SR; wait $SR
# (a run stopped while it places leaves its negotiation's session open, as a killed one does: not counted at the end)
STOPPED=$(s4 "/api/sync/sessions?device_id=$(devof4 a)&limit=1" | jget '[0]["id"]')
cmp -s "$QA" "$QB" && [ ! -e "$OUT/h-a/run/launch.older" ] || bad "slots: a stop during the placing left the mark of a newer save not fetched: $(cat "$OUT/hr-a.log")"
hs a; grep -q 'Peel Quest' "$OUT/hs-a.log" && bad "slots: a save placed under a stop not agreed: $(cat "$OUT/hs-a.log")"
head -c 8192 /dev/urandom > "$QB"; hs b
# (-- before the group: dash otherwise takes "-<pid>" for an option, the kill fails, and the run ends by itself)
slowrun; kill -s KILL -- "-$SR"; wait $SR 2>/dev/null; RC=$?; sleep 0.3
[ "$RC" = 137 ] || bad "slots: the run wasn't killed with its group (exit $RC): $(cat "$OUT/hr-a.log")"
# (its negotiation's session left open too)
KILLED=$(s4 "/api/sync/sessions?device_id=$(devof4 a)&limit=1" | jget '[0]["id"]')
pgrep -f "$OUT/nds-slow" >/dev/null && bad "slots: nds-saves outlived the run killed with its group"
cmp -s "$QA" "$QB" || bad "slots: the kill came before the save was in place: $(cat "$OUT/hr-a.log")"
head -c 8192 /dev/urandom > "$QA"; hs a
grep -q 'up saves/nds/Peel Quest (USA).sav' "$OUT/hs-a.log" && ! grep -q 'conflict.*Peel Quest' "$OUT/hs-a.log" || bad "slots: a game over a save placed by a killed run: $(cat "$OUT/hs-a.log")"
hs b
# save states, when they are on: that game's after it, not before it, and no other game's
ST4=$OUT/h-a/states; mkdir -p "$ST4"; echo 'homesync_states = on' >> "$SA4"
head -c 4096 /dev/urandom > "$ST4/Solo (USA).state"; head -c 4096 /dev/urandom > "$ST4/Other (USA).state"
n=$(logc "POST /api/states"); hr a --auto launch --rom "gba/Solo (USA).gba"
[ "$(logc "POST /api/states")" = "$n" ] || bad "slots: a state sent before playing: $(cat "$OUT/hr-a.log")"
hr a --auto quit --rom "gba/Solo (USA).gba"
[ "$(logc "POST /api/states")" = $((n + 1)) ] && [ "$(logc "POST /api/states?rom_id=$(rid4 "Solo (USA).gba")")" = 1 ] ||
    bad "slots: the states after a game: $(s4 /test/log | grep -o 'POST /api/states[^"]*') $(cat "$OUT/hr-a.log")"
# a state changed here and on the server (another handheld's): held, both kept (the server's in the vault, fetched
# once), neither sent over nor taken, nothing beside the state, and never the newer by the files' times
head -c 4096 /dev/urandom > "$ST4/Solo (USA).state"; cp "$ST4/Solo (USA).state" "$OUT/solo.state"; past "$ST4/Solo (USA).state"
head -c 4096 /dev/urandom > "$OUT/srv.state"
curl -s -XPOST "$U4/test/save" -d "{\"kind\": \"states\", \"rom_id\": $(rid4 "Solo (USA).gba"), \"file_name\": \"Solo (USA).state\", \"emulator\": \"retroarch\", \"data\": \"$(hex "$OUT/srv.state")\", \"updated_at\": \"$(date -u -d "@$(( $(date +%s) + 5 ))" +%Y-%m-%dT%H:%M:%SZ)\"}" >/dev/null
sid=$(s4 "/api/states?platform_id=$(s4 /api/platforms | python3 -c 'import json,sys; print(next(p["id"] for p in json.load(sys.stdin) if p["fs_slug"] == "gba"))')" | python3 -c 'import json,sys; print(next(s["id"] for s in json.load(sys.stdin) if s["file_name"] == "Solo (USA).state"))')
n=$(logc "POST /api/states"); hr a --auto quit --rom "gba/Solo (USA).gba"
SK="$(rid4 "Solo (USA).gba")/state:Solo (USA).state"
grep -q 'conflict, both kept: states/Solo (USA).state (the server.s in the vault)' "$OUT/hr-a.log" && cmp -s "$ST4/Solo (USA).state" "$OUT/solo.state" &&
    [ "$(logc "POST /api/states")" = "$n" ] && [ "$(rs a 'c["held"]["'"$SK"'"]["server_md5"]')" = "$(m "$OUT/srv.state")" ] && [ -z "$(ls "$ST4" | grep '\.bak$')" ] &&
    ls "$OUT/h-a/banana/save-vault/states/Solo (USA)/" | grep -q -- "-$(m "$OUT/srv.state" | cut -c1-8)\.state$" ||
    bad "slots: a state changed on both sides: $(cat "$OUT/hr-a.log") $(rs a 'c["held"]')"
n=$(logc "GET /api/states/$sid/content"); hr a --auto quit --rom "gba/Solo (USA).gba"
grep -q 'conflict, both kept: states/Solo (USA).state' "$OUT/hr-a.log" && [ "$(logc "GET /api/states/$sid/content")" = "$n" ] && [ "$(rs a '"'"$SK"'" in c["held"]')" = True ] ||
    bad "slots: a held state, the next sync: fetched again or lost: $(cat "$OUT/hr-a.log")"
# (a full sync, which works out the saves' records afresh, leaves a state's alone; settled by hand, it goes)
hs a; [ "$(rs a '"'"$SK"'" in c["held"]')" = True ] || bad "slots: a full sync dropped a held state's record"
cp "$OUT/srv.state" "$ST4/Solo (USA).state"; hr a --auto quit --rom "gba/Solo (USA).gba"
[ "$(rs a '"'"$SK"'" in c["held"]')" = False ] && ! grep -q 'conflict, both kept' "$OUT/hr-a.log" || bad "slots: a state settled by hand still held: $(cat "$OUT/hr-a.log")"
# a state from the server is never placed: another handheld's new version of one this card holds, unchanged here, or of
# one it doesn't, goes to the vault once, and the card's states stay as they are
cp "$ST4/Solo (USA).state" "$OUT/solo2.state"; head -c 4096 /dev/urandom > "$OUT/srv2.state"
sstate() { curl -s -XPOST "$U4/test/save" -d "{\"kind\": \"states\", \"rom_id\": $(rid4 "$1.gba"), \"file_name\": \"$1.state\", \"emulator\": \"retroarch\", \"data\": \"$(hex "$2")\", \"updated_at\": \"$(date -u -d "@$(( $(date +%s) + $3 ))" +%Y-%m-%dT%H:%M:%SZ)\"}" >/dev/null; }
sstate "Solo (USA)" "$OUT/srv2.state" 10
VS=$OUT/h-a/banana/save-vault/states
hr a --auto quit --rom "gba/Solo (USA).gba"
cmp -s "$ST4/Solo (USA).state" "$OUT/solo2.state" && ls "$VS/Solo (USA)/" | grep -q -- "-$(m "$OUT/srv2.state" | cut -c1-8)\.state$" && grep -q 'kept in the vault, not placed: states/Solo (USA).state' "$OUT/hr-a.log" ||
    bad "slots: a server's state placed, or not kept: $(cat "$OUT/hr-a.log")"
n=$(logc "GET /api/states/$sid/content"); hr a --auto quit --rom "gba/Solo (USA).gba"
[ "$(logc "GET /api/states/$sid/content")" = "$n" ] || bad "slots: a state kept in the vault was fetched again: $(cat "$OUT/hr-a.log")"
rm "$ST4/Solo (USA).state"; hr a --auto quit --rom "gba/Solo (USA).gba"; n=$(logc "GET /api/states/$sid/content"); hr a --auto quit --rom "gba/Solo (USA).gba"
[ ! -e "$ST4/Solo (USA).state" ] && [ "$(logc "GET /api/states/$sid/content")" = "$n" ] || bad "slots: a state deleted here came back, or is fetched at each sync: $(cat "$OUT/hr-a.log")"
# a held state settled on the Saves page: the card's goes up by its name, the server's stays in the vault; the server's
# is placed only then, by that choice
head -c 4096 /dev/urandom > "$ST4/Solo (USA).state"; hr a --auto quit --rom "gba/Solo (USA).gba"
head -c 4096 /dev/urandom > "$ST4/Solo (USA).state"; cp "$ST4/Solo (USA).state" "$OUT/s-card.state"; head -c 4096 /dev/urandom > "$OUT/s-srv.state"
sstate "Solo (USA)" "$OUT/s-srv.state" 20; hr a --auto quit --rom "gba/Solo (USA).gba"
[ "$(rs a '"'"$SK"'" in c["held"]')" = True ] || bad "slots: no state held: $(cat "$OUT/hr-a.log")"
sed -i '/^homesync_states/d' "$SA4"     # (a choice sends it whatever the setting says)
[ "$(rv a resolve "$SK" card)" = ok ] && [ "$(s4 "/api/states/$sid/content" | md5sum | cut -c1-32)" = "$(m "$OUT/s-card.state")" ] && [ "$(rs a '"'"$SK"'" in c["held"]')" = False ] ||
    bad "slots: a held state, this handheld's: $(cat "$OUT/h-a/banana/logs/sync/"*.log 2>/dev/null | tail -3) $(rs a 'c["held"]')"
echo 'homesync_states = on' >> "$SA4"
head -c 4096 /dev/urandom > "$ST4/Solo (USA).state"; cp "$ST4/Solo (USA).state" "$OUT/s-card.state"; head -c 4096 /dev/urandom > "$OUT/s-srv.state"
sstate "Solo (USA)" "$OUT/s-srv.state" 30; hr a --auto quit --rom "gba/Solo (USA).gba"
[ "$(rv a resolve "$SK" server)" = ok ] && cmp -s "$ST4/Solo (USA).state" "$OUT/s-srv.state" && ls "$VS/Solo (USA)/" | grep -q -- "-$(m "$OUT/s-card.state" | cut -c1-8)\.state$" ||
    bad "slots: a held state, the server's: $(rs a 'c["held"]')"
hr a --auto quit --rom "gba/Solo (USA).gba"; [ "$(rs a '"'"$SK"'" in c["held"]')" = False ] && ! grep -q 'conflict, both kept' "$OUT/hr-a.log" || bad "slots: a state settled for the server's held again: $(cat "$OUT/hr-a.log")"
# the card's kept over the server's, the answer lost (the server took it, then answered 500): the server's copy in the
# vault may now be the only one, and stays there
head -c 4096 /dev/urandom > "$ST4/Solo (USA).state"; head -c 4096 /dev/urandom > "$OUT/s-srv.state"; sstate "Solo (USA)" "$OUT/s-srv.state" 35; hr a --auto quit --rom "gba/Solo (USA).gba"
PV=$(rs a 'c["held"]["'"$SK"'"]["vault"]')
curl -s -XPOST "$U4/test/fault" -d '{"path": "POST /api/states", "kind": "500-after"}' >/dev/null; rv a resolve "$SK" card >/dev/null
# (the next sync finds the card's there after all, so nothing names the copy any more; it stays)
hr a --auto quit --rom "gba/Solo (USA).gba"
[ "$(rs a '"'"$SK"'" in c["held"]')" = False ] && [ -e "$PV" ] || bad "slots: the server's copy a lost answer replaced, gone: $(cat "$OUT/hr-a.log")"
# (the copy in the vault gone: not sent over the server's, which it would replace. A failed fetch keeps what was held)
head -c 4096 /dev/urandom > "$ST4/Solo (USA).state"; head -c 4096 /dev/urandom > "$OUT/s-srv.state"; sstate "Solo (USA)" "$OUT/s-srv.state" 40; hr a --auto quit --rom "gba/Solo (USA).gba"
SV=$(rs a 'c["held"]["'"$SK"'"]["vault"]'); [ -f "$SV" ] && mv "$SV" "$OUT/sv.keep"
n=$(logc "POST /api/states"); rv a resolve "$SK" card | grep -q "isn't in the vault" && [ "$(logc "POST /api/states")" = "$n" ] && [ "$(rs a '"'"$SK"'" in c["held"]')" = True ] || bad "slots: a state kept with its server copy gone: $(rs a 'c["held"]')"
mv "$OUT/sv.keep" "$SV"; sstate "Solo (USA)" "$OUT/s-srv.state" 50
curl -s -XPOST "$U4/test/fault" -d '{"path": "GET /api/states/[0-9]+/content", "kind": "500"}' >/dev/null
hr a --auto quit --rom "gba/Solo (USA).gba"; [ "$(rs a '"'"$SK"'" in c["held"]')" = True ] || bad "slots: a failed fetch dropped a held state: $(cat "$OUT/hr-a.log")"
curl -s -XPOST "$U4/test/fault" -d '{"clear": true}' >/dev/null
sed -i '/^homesync_states/d' "$SA4"; rm -f "$ST4/Solo (USA).state" "$ST4/Other (USA).state"
# Wi-Fi back with Auto sync only "Leaving a game": the saves waiting to be sent go, nothing else (no game copied, no
# other game's save); none waiting, nothing asked of the server
head -c 8192 /dev/urandom > "$GA/Solo (USA).srm"; head -c 8192 /dev/urandom > "$GA/Other (USA).srm"; o=$(head4 "Other (USA).gba" | cut -d' ' -f1)
curl -s -XPOST "$U4/test/fault" -d '{"path": "POST /api/saves$", "kind": "500"}' >/dev/null; hr a --auto quit --rom "gba/Solo (USA).gba"
[ "$(rs a 'list(c["queue"])')" = "['$(rid4 "Solo (USA).gba")/autosave']" ] || bad "slots: Wi-Fi: no save waiting: $(cat "$OUT/hr-a.log")"
hr a --auto wifi
[ "$(head4 "Solo (USA).gba" | cut -d' ' -f1)" = "$(m "$GA/Solo (USA).srm")" ] && [ "$(head4 "Other (USA).gba" | cut -d' ' -f1)" = "$o" ] &&
    [ "$(rs a 'len(c["queue"])')" = 0 ] && ! grep -q '^roms: ' "$OUT/hr-a.log" || bad "slots: Wi-Fi sending what waits: $(cat "$OUT/hr-a.log")"
n=$(logc "GET /api/platforms"); hr a --auto wifi
[ "$(logc "GET /api/platforms")" = "$n" ] || bad "slots: Wi-Fi with nothing waiting asked the server: $(cat "$OUT/hr-a.log")"
# (a game started while the waiting saves go: the rest stay waiting)
head -c 8192 /dev/urandom > "$GA/Solo (USA).srm"; head -c 8192 /dev/urandom > "$GA/Other (USA).srm"; o=$(head4 "Solo (USA).gba" | cut -d' ' -f1)
curl -s -XPOST "$U4/test/fault" -d '{"path": "POST /api/saves$", "kind": "500"}' >/dev/null; hr a --auto quit --rom "gba/Solo (USA).gba"
curl -s -XPOST "$U4/test/fault" -d '{"path": "POST /api/saves$", "kind": "500"}' >/dev/null; hr a --auto quit --rom "gba/Other (USA).gba"
curl -s -XPOST "$U4/test/fault" -d '{"path": "POST /api/saves$", "kind": "delay", "arg": 3}' >/dev/null
hr a --auto wifi & _k=$!; sleep 1; sleep 30 & _g=$!; echo $_g > "$OUT/h-a/run/game.pid"; wait $_k; kill $_g; rm "$OUT/h-a/run/game.pid"
[ "$(head4 "Solo (USA).gba" | cut -d' ' -f1)" = "$o" ] && [ "$(rs a 'len(c["queue"])')" = 1 ] ||
    bad "slots: a game started during the Wi-Fi queue: $(rs a 'list(c["queue"])') $(cat "$OUT/hr-a.log")"
hr a --auto wifi
sed -i 's/^homesync_auto = quit$/homesync_auto = off/' "$SA4"
# a game started during a sync of every save: the games not reached yet and the states are left alone, and go with the
# next sync
head -c 8192 /dev/urandom > "$GA/Solo (USA).srm"; head -c 8192 /dev/urandom > "$GA/Other (USA).srm"; o=$(head4 "Solo (USA).gba" | cut -d' ' -f1)
echo 'homesync_states = on' >> "$SA4"; head -c 4096 /dev/urandom > "$ST4/Solo (USA).state"; n=$(logc "POST /api/states")
curl -s -XPOST "$U4/test/fault" -d '{"path": "POST /api/saves$", "kind": "delay", "arg": 3}' >/dev/null
hs a & _k=$!; sleep 1; sleep 30 & _g=$!; echo $_g > "$OUT/h-a/run/game.pid"; wait $_k
[ "$(head4 "Solo (USA).gba" | cut -d' ' -f1)" = "$o" ] && [ "$(head4 "Other (USA).gba" | cut -d' ' -f1)" = "$(m "$GA/Other (USA).srm")" ] &&
    [ "$(logc "POST /api/states")" = "$n" ] && [ "$(grep -c 'error: saves: a game started, the rest next time' "$OUT/hs-a.log")" = 1 ] ||
    bad "slots: a game started during a sync: $(cat "$OUT/hs-a.log")"
sed -i '/^homesync_states/d' "$SA4"; rm "$ST4/Solo (USA).state"
kill $_g; rm "$OUT/h-a/run/game.pid"; hs a
[ "$(head4 "Solo (USA).gba" | cut -d' ' -f1)" = "$(m "$GA/Solo (USA).srm")" ] || bad "slots: the save left when a game started: $(cat "$OUT/hs-a.log")"
# the card's sync state as it was before its saves went by slot, kept beside the new one; every negotiation's
# session completed
v1=$OUT/h-a/banana/romsync.v1.json
[ -f "$v1" ] && [ "$(stat -c %a "$v1")" = 600 ] && ! grep -q '"stateVersion"\|"slots"' "$v1" && grep -q "$(jget '["token"]' < "$OUT/h-a.first.json")" "$v1" && [ "$(rs a 'c["stateVersion"]')" = 2 ] ||
    bad "slots: romsync.v1.json isn't the file before: $(cat "$v1")"
s4 "/api/sync/sessions?device_id=$(devof4 a)&limit=500" | python3 -c 'import json,sys; x = json.load(sys.stdin); sys.exit(0 if x and all(s["status"] == "COMPLETED" or str(s["id"]) in sys.argv[1:] for s in x) else 1)' "$LOST" "$STOPPED" "$KILLED" ||
    bad "slots: a session left open: $(s4 "/api/sync/sessions?device_id=$(devof4 a)&limit=500")"
grep -q "negotiation's own plan: .*(not followed)" "$OUT/hs-a.log" || bad "slots: negotiation's plan not logged: $(cat "$OUT/hs-a.log")"
[ "$(curl -s "$U4/test/violations")" = "[]" ] || bad "slots: a card sent what it never may: $(curl -s "$U4/test/violations")"

# ---------------------------------------------------------------- the slot Settings names (homesync_slot = custom,
# homesync_slot_name): saves sync with that slot alone, and autosave's devices never see them; a slot changed carries a
# save waiting to the new slot and lets go of what was held with the old one, its copy kept in the vault; the versions
# agreed stay per slot, so going back finds them; a name that isn't a slot's is said, and autosave used. A stand-in of
# its own, every card's token strict
L5=$OUT/lib5
mkdir -p "$L5/gba"; for g in Den Held Wait; do printf 'gba rom %s' "$g" > "$L5/gba/$g (USA).gba"; done
P5=$(port)
python3 "$HERE/tests/fake-romm.py" "$P5" "$L5" 2> "$OUT/fake-romm5.log" & PIDS="$PIDS $!"
waitport "$P5" /api/heartbeat || bad "the custom slot's fake RomM didn't start"
U5=http://127.0.0.1:$P5
TA5=$(curl -s -XPOST "$U5/test/token" -d '{}' | jget '["token"]')
s5() { curl -s -H "Authorization: Bearer $TA5" "$U5$1"; }
TOKS5=""
hand5() {
    _h=$OUT/s-$1; realcard "$_h"; cp "$L5/gba/"* "$_h/roms/gba/"
    _t=$(curl -s -XPOST "$U5/test/token" -d '{}' | jget '["token"]')
    TOKS5="$TOKS5${TOKS5:+, }\"$_t\""; curl -s -XPOST "$U5/test/strict" -d "{\"tokens\": [$TOKS5]}" >/dev/null
    printf '{"backend": "romm", "server": "%s", "token": "%s"}\n' "$U5" "$_t" > "$_h/banana/romsync.json"
}
ss() { _n=$1; shift; "$BIN" --root "$OUT/s-$_n" --run "$OUT/s-$_n/run" --status "$OUT/s-$_n/run/sync.status" "$@" > "$OUT/ss-$_n.log" 2>&1; }
rid5() { s5 "/api/roms?platform_ids=$(s5 /api/platforms | jget '[0]["id"]')&limit=500" | python3 -c "import json,sys; print(next(r['id'] for r in json.load(sys.stdin)['items'] if r['fs_name'] == sys.argv[1]))" "$1 (USA).gba"; }
# head5 <game> <slot>: that slot's head's MD5, "none" without one
head5() { s5 "/api/saves?rom_id=$(rid5 "$1")&slot=$(python3 -c 'import sys, urllib.parse; print(urllib.parse.quote(sys.argv[1]))' "$2")" | python3 -c '
import json, sys
rows = json.load(sys.stdin)
print(max(rows, key=lambda s: (s["updated_at"], s["id"]))["content_hash"] if rows else "none")'; }
rs5() { python3 -c "import json,sys; c = json.load(open(sys.argv[1])); print($2)" "$OUT/s-$1/banana/romsync.json"; }
slot5() { sed -i '/^homesync_slot/d' "$OUT/s-$1/banana/settings.conf"; [ -z "$2" ] || printf 'homesync_slot = custom\nhomesync_slot_name = %s\n' "$2" >> "$OUT/s-$1/banana/settings.conf"; }
hand5 e; hand5 g; E5=$OUT/s-e/saves/gba; G5=$OUT/s-g/saves/gba
slot5 e "Den Slot"
head -c 8192 /dev/urandom > "$E5/Den (USA).srm"; cp "$E5/Den (USA).srm" "$OUT/den1.srm"
ss e sync --saves-only
grep -q 'save slot: Den Slot' "$OUT/ss-e.log" && [ "$(head5 Den "Den Slot")" = "$(m "$OUT/den1.srm")" ] && [ "$(head5 Den autosave)" = none ] ||
    bad "custom slot: not sent to it alone: $(head5 Den "Den Slot") $(head5 Den autosave): $(cat "$OUT/ss-e.log")"
ss g sync --saves-only
[ -e "$G5/Den (USA).srm" ] && bad "custom slot: an autosave handheld took a custom slot's save: $(cat "$OUT/ss-g.log")"
# back to autosave: the card's save never agreed with it goes to an empty autosave, and the autosave handheld takes it
slot5 e; ss e sync --saves-only
[ "$(head5 Den autosave)" = "$(m "$OUT/den1.srm")" ] || bad "custom slot: back to autosave, not sent there: $(cat "$OUT/ss-e.log")"
ss g sync --saves-only; cmp -s "$G5/Den (USA).srm" "$OUT/den1.srm" || bad "custom slot: the autosave handheld didn't get it: $(cat "$OUT/ss-g.log")"
# and to the custom slot again: agreed there still, nothing moves
slot5 e "Den Slot"; ss e sync --saves-only
grep -q " 0 saves up, 0 down, 0 conflicts, 0 errors" "$OUT/ss-e.log" || bad "custom slot: going back lost the agreement: $(cat "$OUT/ss-e.log")"
# a save waiting to be sent to the custom slot (the server refused it after a game) goes to autosave once that's the slot
printf 'homesync_auto = quit\n' >> "$OUT/s-e/banana/settings.conf"
head -c 8192 /dev/urandom > "$E5/Wait (USA).srm"; cp "$E5/Wait (USA).srm" "$OUT/wait.srm"
curl -s -XPOST "$U5/test/fault" -d '{"path": "POST /api/saves$", "kind": "500"}' >/dev/null
ss e sync --auto quit --rom "gba/Wait (USA).gba"
[ "$(rs5 e 'list(c["queue"])')" = "['$(rid5 Wait)/Den Slot']" ] || bad "custom slot: the refused save isn't waiting for its slot: $(rs5 e 'c["queue"]'): $(cat "$OUT/ss-e.log")"
slot5 e; ss e sync --auto wifi
grep -q 'slot Den Slot to autosave: a save waiting to be sent goes to autosave' "$OUT/ss-e.log" && [ "$(head5 Wait autosave)" = "$(m "$OUT/wait.srm")" ] &&
    [ "$(head5 Wait "Den Slot")" = none ] && [ "$(rs5 e 'len(c["queue"])')" = 0 ] ||
    bad "custom slot: the waiting save didn't follow the slot: $(rs5 e 'c["queue"]'): $(cat "$OUT/ss-e.log")"
# a conflict held with the custom slot: not listed once Settings names another, and let go by the next sync, said, its
# copy staying in the vault
slot5 e "Den Slot"; head -c 8192 /dev/urandom > "$E5/Held (USA).srm"; ss e sync --saves-only
curl -s -XPOST "$U5/test/save" -d "{\"rom_id\": $(rid5 Held), \"slot\": \"Den Slot\", \"file_name\": \"Held (USA).srm\", \"data\": \"$(head -c 8192 /dev/urandom | od -An -v -tx1 | tr -d ' \n')\", \"updated_at\": \"$(date -u -d "@$(( $(date +%s) + 5 ))" +%Y-%m-%dT%H:%M:%SZ)\"}" >/dev/null
head -c 8192 /dev/urandom > "$E5/Held (USA).srm"; ss e sync --saves-only
grep -q 'conflict, both kept: saves/gba/Held (USA).srm' "$OUT/ss-e.log" || bad "custom slot: no conflict to let go of: $(cat "$OUT/ss-e.log")"
HV=$(rs5 e '[v["vault"] for v in c["conflicts"].values()][0]')
slot5 e
"$BIN" --root "$OUT/s-e" --run "$OUT/s-e/run" saves > "$OUT/ss-e-saves.txt" 2>/dev/null
grep -q '^conflict' "$OUT/ss-e-saves.txt" && bad "custom slot: a conflict with the slot no longer named still listed: $(cat "$OUT/ss-e-saves.txt")"
ss e sync --saves-only
grep -q 'slot Den Slot to autosave: saves/gba/Held (USA).srm with Den Slot no longer held' "$OUT/ss-e.log" && [ -n "$HV" ] && [ -e "$HV" ] &&
    [ "$(rs5 e 'sum(1 for k in list(c["conflicts"]) + list(c["held"]) if k.endswith("/Den Slot"))')" = 0 ] ||
    bad "custom slot: the old slot's conflict not let go, or its copy gone: $HV: $(rs5 e 'c["conflicts"]'): $(cat "$OUT/ss-e.log")"
# a name no slot can have: said, and autosave it is
slot5 e "bad:name"; head -c 8192 /dev/urandom > "$E5/Den (USA).srm"; ss e sync --saves-only
grep -q "save slot: \"bad:name\" isn't a slot name" "$OUT/ss-e.log" && [ "$(head5 Den autosave)" = "$(m "$E5/Den (USA).srm")" ] ||
    bad "custom slot: a bad name not said, or not autosave: $(cat "$OUT/ss-e.log")"
[ "$(curl -s "$U5/test/violations")" = "[]" ] || bad "custom slot: a card sent what it never may: $(curl -s "$U5/test/violations")"

# no traceback but those of the faults that asked the handler to fail: each traceback's last line, the exception, is
# what tells them apart
for f in "$OUT/fake-romm.log" "$OUT/fake-romm2.log" "$OUT/fake-romm3.log" "$OUT/fake-romm4.log" "$OUT/fake-romm5.log"; do
    u=$(awk '/^Traceback/ { t = 1; next } t && !/^ / { t = 0; if ($0 !~ /^RuntimeError: a fault asked for on/) print }' "$f")
    [ -z "$u" ] || bad "the stand-in failed a request ($f): $u"
done

[ -s "$OUT/failures" ] || { echo "sync-slots: all checks passed"; exit 0; }
echo "sync-slots: $(wc -l < "$OUT/failures") checks failed ($OUT/failures)"
exit 1

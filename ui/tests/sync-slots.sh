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
# stand-in's faults (119, 120), an NDS game
L=$OUT/lib
mkdir -p "$L/gba" "$L/nds"
for i in $(seq 20); do printf 'gba rom %s' "$i" > "$L/gba/Game $i (USA).gba"; done
python3 -c 'import sys, zipfile; z = zipfile.ZipFile(sys.argv[1], "w", zipfile.ZIP_DEFLATED); z.writestr("Zip Game (USA).gba", b"gba rom zipped " * 300); z.writestr("Zip Game (USA) [b].gba", b"second member" * 10)' "$L/gba/Zip Game (USA).zip"
head -c 4096 /dev/urandom > "$L/nds/Peel Quest (USA).nds"
P=$(port)
python3 "$HERE/tests/fake-romm.py" "$P" "$L" 2> "$OUT/fake-romm.log" & PIDS="$PIDS $!"
waitport "$P" /api/heartbeat || bad "fake RomM didn't start: $(tail -3 "$OUT/fake-romm.log")"
U=http://127.0.0.1:$P

# ---------------------------------------------------------------- the stand-in is RomM 5.4.1
python3 "$HERE/tests/romm-conformance.py" "$U" > "$OUT/conformance.log" 2>&1 || bad "the stand-in isn't RomM 5.4.1: $(grep FAIL "$OUT/conformance.log")"
if [ -n "$ROMM_URL" ] && [ -n "$ROMM_TOKENS" ]; then
    python3 "$HERE/tests/romm-conformance.py" "$ROMM_URL" "$ROMM_TOKENS" > "$OUT/conformance-real.log" 2>&1 ||
        bad "the real RomM at $ROMM_URL differs: $(grep FAIL "$OUT/conformance-real.log")"
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
for n in g1 g2 b; do curl -s -XPOST "$U2/test/token" -d '{}' | jget '["token"]' > "$OUT/$n.token"; done
peer() { n=$1; c=$2; shift 2; python3 "$HERE/tests/romm-peers.py" "$U2" "$(cat "$OUT/$n.token")" "$c" "$OUT/$n" "$@" > "$OUT/$n.json" 2>> "$OUT/peers.err"; }
said() { jget "$2" < "$OUT/$1.json"; }
srv() { curl -s -H "Authorization: Bearer $(cat "$OUT/b.token")" "$U2$1"; }
headof() { srv "/api/saves?rom_id=$1&slot=autosave" | python3 -c 'import json,sys; r=json.load(sys.stdin); print(max(r, key=lambda s: (s["updated_at"], s["id"]))["id"] if r else "")'; }
GS="Saves/GBA/Banana Run (USA).gba.sav"
# a Grout save edited, dated 2 s ahead: Grout compares whole-second mtimes, so an edit in the second of its last sync
# looks unchanged to negotiation until the next second
edit() { head -c 8192 /dev/urandom > "$1"; touch -d "@$(( $(date +%s) + 2 ))" "$1"; }
mkdir -p "$OUT/g1/Saves/GBA"; head -c 8192 /dev/urandom > "$OUT/g1/$GS"
peer g1 grout sync
[ "$(said g1 '["uploaded"]')" = "['Banana Run (USA).gba.sav']" ] || bad "Grout didn't upload its save: $(cat "$OUT/g1.json")"
srv "/api/saves?rom_id=101" | python3 -c 'import json,re,sys; r=json.load(sys.stdin); sys.exit(0 if len(r) == 1 and r[0]["emulator"] == "GBA" and r[0]["slot"] == "autosave" and re.fullmatch(r"Banana Run \(USA\)\.gba \[[0-9_-]+\]\.sav", r[0]["file_name"]) else 1)' ||
    bad "Grout's version: $(srv "/api/saves?rom_id=101")"
peer g2 grout sync
cmp -s "$OUT/g1/$GS" "$OUT/g2/$GS" || bad "a second Grout didn't get the save: $(cat "$OUT/g2.json")"
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
${CXX:-g++} -std=c++17 -O2 -Wall -o "$BIN" "$HERE/src/sync.cpp" "$HERE/src/sync_core.cpp" "$HERE/src/sync_romm.cpp" \
    "$HERE/src/save_adapt.cpp" "$HERE/src/save_paths.cpp" "$HERE/src/save_vault.cpp" "$HERE/src/json.cpp" -ldl || bad "banana-sync doesn't build"
L3=$OUT/lib3
mkdir -p "$L3/gba"
for g in "Bad Hash" "Blank Card" "Blank Only" "Blanked" "Blank Server" "Full Disk" "Killed" "Truncated" "Unsafe" \
    "Other Bytes" "Two Cores" "Flat" "Sorted Off" "Grout Same" "Grout New" "Padded" "Tail" "Zipped" "Too Big" "Clock"; do printf 'gba rom %s' "$g" > "$L3/gba/$g (USA).gba"; done
mkdir -p "$L3/snes" "$L3/nds"; printf 'snes rom' > "$L3/snes/Peel Kart (USA).sfc"; printf 'nds rom' > "$L3/nds/Peel Quest (USA).nds"; printf 'nds rom two' > "$L3/nds/Peel Two (USA).nds"
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
cmp -s "$K" "$OUT/kart.srm" || bad "a SNES save from the server didn't go into Snes9x's folder: $(cat "$OUT/bs.log")"
[ -e "$C/saves/Peel Kart (USA).srm" ] && bad "a SNES save went into the flat saves/"
# the same name, another dump: not the server's game, its save not sent, and said
head -c 8192 /dev/urandom > "$S/Other Bytes (USA).srm"; bs
[ "$(saves3 "Other Bytes")" = 0 ] || bad "a save of another dump of the game was sent"
grep -q "not synced, not the server's ROM (other bytes): gba/Other Bytes (USA).gba" "$OUT/bs.log" && grep -q '1 game not matched on the server' "$OUT/bs.log" ||
    bad "another dump not said: $(cat "$OUT/bs.log")"
# games chosen for syncing (homesync_games = chosen): an unchosen game's save neither sent nor taken, a chosen one's sent
head -c 8192 /dev/urandom > "$S/Not Chosen (USA).srm"; cp "$S/Not Chosen (USA).srm" "$OUT/nc.srm"; head -c 8192 /dev/urandom > "$S/Zip Game (USA).srm"
printf 'homesync_games = chosen\n' >> "$C/banana/settings.conf"; printf 'gba/Zip Game (USA).zip\n' > "$C/banana/homesync-games.txt"; bs
[ "$(saves3 "Not Chosen")" = 0 ] && [ "$(nsaves "Zip Game (USA).zip")" = 1 ] || bad "chosen games: unchosen $(saves3 "Not Chosen"), chosen $(nsaves "Zip Game (USA).zip") sent: $(cat "$OUT/bs.log")"
head -c 8192 /dev/urandom > "$OUT/nc2.srm"; seed "Not Chosen" "$(hex "$OUT/nc2.srm")" 5 >/dev/null; bs
cmp -s "$S/Not Chosen (USA).srm" "$OUT/nc.srm" || bad "an unchosen game's save taken from the server"
sed -i '/^homesync_games = /d' "$C/banana/settings.conf"; rm -f "$C/banana/homesync-games.txt" "$S/Not Chosen (USA).srm" "$C/roms/gba/Not Chosen (USA).gba"
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
# brought into step with each. A card whose raw save is the .dsv's agrees with it at once; one of another size than
# the card's is held, kept in the vault, never put in place
# (the stand-in for nds-saves says too which of the two copies it would take: the newer)
cat > "$OUT/nds-saves3" <<EOF
#!/bin/sh
echo "\$1" >> "$OUT/nds-saves3.log"
G=\${1##*/}; G=\${G%.*}
[ -f "$C/drastic/backup/\$G.dsv" ] || exit 0
if [ "$C/saves/nds/\$G.sav" -nt "$C/drastic/backup/\$G.dsv" ]; then echo "\$G: sav-newer"; else echo "\$G: dsv-newer"; fi >> "$OUT/nds-saves3.log"
EOF
chmod +x "$OUT/nds-saves3"
Q=$C/saves/nds/"Peel Quest (USA).sav"
head -c 8192 /dev/urandom > "$OUT/qd.sav"; dsv "$OUT/qd.sav" "$OUT/qd.dsv"; seedf "Peel Quest (USA).nds" "Peel Quest (USA).dsv" "$(hex "$OUT/qd.dsv")" 0
BANANA_NDS_SAVES=$OUT/nds-saves3 bs
cmp -s "$Q" "$OUT/qd.sav" || bad "a server .dsv not taken as its raw save: $(cat "$OUT/bs.log")"
[ -z "$(find "$C/saves" -name '*.dsv')" ] || bad "a server .dsv was put in saves/: $(find "$C/saves" -name '*.dsv')"
grep -qx "$C/roms/nds/Peel Quest (USA).nds" "$OUT/nds-saves3.log" 2>/dev/null || bad "nds-saves wasn't run after a .dsv was installed"
BANANA_NDS_SAVES=$OUT/nds-saves3 bs
grep -q 'Peel Quest' "$OUT/bs.log" && bad "a .dsv taken once, sent or fetched again: $(cat "$OUT/bs.log")"
[ "$(nsaves "Peel Quest (USA).nds")" = 1 ] || bad "a .dsv taken, then the card's raw save sent back: $(nsaves "Peel Quest (USA).nds") versions"
# (DraStic's copy dated a minute ahead, as a card clock ahead of the server's leaves it: the save put in place is still
# the newer when nds-saves runs after it, so the .dsv isn't written back over it)
head -c 8192 /dev/urandom > "$OUT/quest.sav"; seedf "Peel Quest (USA).nds" "Peel Quest (USA).sav" "$(hex "$OUT/quest.sav")" 5
cp "$Q" "$C/drastic/backup/Peel Quest (USA).dsv"; touch -d "@$(( $(date +%s) + 60 ))" "$C/drastic/backup/Peel Quest (USA).dsv"
BANANA_NDS_SAVES=$OUT/nds-saves3 bs
cmp -s "$Q" "$OUT/quest.sav" || bad "a DS save from the server: $(cat "$OUT/bs.log")"
[ "$(grep 'Peel Quest (USA): ' "$OUT/nds-saves3.log" | tail -1)" = 'Peel Quest (USA): sav-newer' ] ||
    bad "a DS save from the server older than DraStic's copy when nds-saves ran: $(cat "$OUT/nds-saves3.log")"
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
grep -q 'the server.s newest is this same save in another form: saves/nds/Peel Two' "$OUT/bs.log" && ! grep -q 'conflict, both kept' "$OUT/bs.log" ||
    bad "the card's raw save under a .dsv of the same bytes not agreed: $(cat "$OUT/bs.log")"
BANANA_NDS_SAVES=$OUT/nds-saves3 bs
grep -q 'Peel Two' "$OUT/bs.log" && bad "a .dsv agreed with, looked at again: $(cat "$OUT/bs.log")"
[ "$(nsaves "Peel Two (USA).nds")" = 1 ] || bad "a .dsv agreed with, then the card's raw save sent: $(nsaves "Peel Two (USA).nds") versions"

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
for g in Shared Gone "Blank Head" Skew Dry Joiner Lone Race "Race Same" "Race Size" Queued "Queued Gone" Refetch Kept Diverged Closed; do printf 'gba rom %s' "$g" > "$L4/gba/$g (USA).gba"; done
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
echo more >> "$GA/Gone (USA).srm"; hs a; [ "$(nv4 "Gone (USA).gba")" = 1 ] || bad "slots: a change after a server delete wasn't sent: $(cat "$OUT/hs-a.log")"
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
grep -q " 1 conflicts" "$OUT/hs-a.log" && [ "$(head4 "Race (USA).gba" | cut -d' ' -f1)" = "$(m "$OUT/peer.srm")" ] && [ "$(rs a 'len([k for k in c["conflicts"] if k.startswith("'"$(rid4 "Race (USA).gba")"'/")])')" = 1 ] ||
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
# the card's sync state as it was before its saves went by slot, kept beside the new one; every negotiation's
# session completed
v1=$OUT/h-a/banana/romsync.v1.json
[ -f "$v1" ] && [ "$(stat -c %a "$v1")" = 600 ] && ! grep -q '"stateVersion"\|"slots"' "$v1" && grep -q "$(jget '["token"]' < "$OUT/h-a.first.json")" "$v1" && [ "$(rs a 'c["stateVersion"]')" = 2 ] ||
    bad "slots: romsync.v1.json isn't the file before: $(cat "$v1")"
s4 "/api/sync/sessions?device_id=$(devof4 a)&limit=500" | python3 -c 'import json,sys; x = json.load(sys.stdin); sys.exit(0 if x and all(s["status"] == "COMPLETED" or str(s["id"]) == sys.argv[1] for s in x) else 1)' "$LOST" ||
    bad "slots: a session left open: $(s4 "/api/sync/sessions?device_id=$(devof4 a)&limit=500")"
grep -q "negotiation's own plan: .*(not followed)" "$OUT/hs-a.log" || bad "slots: negotiation's plan not logged: $(cat "$OUT/hs-a.log")"
[ "$(curl -s "$U4/test/violations")" = "[]" ] || bad "slots: a card sent what it never may: $(curl -s "$U4/test/violations")"

# no traceback but those of the faults that asked the handler to fail: each traceback's last line, the exception, is
# what tells them apart
for f in "$OUT/fake-romm.log" "$OUT/fake-romm2.log" "$OUT/fake-romm3.log" "$OUT/fake-romm4.log"; do
    u=$(awk '/^Traceback/ { t = 1; next } t && !/^ / { t = 0; if ($0 !~ /^RuntimeError: a fault asked for on/) print }' "$f")
    [ -z "$u" ] || bad "the stand-in failed a request ($f): $u"
done

[ -s "$OUT/failures" ] || { echo "sync-slots: all checks passed"; exit 0; }
echo "sync-slots: $(wc -l < "$OUT/failures") checks failed ($OUT/failures)"
exit 1

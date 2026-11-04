#!/bin/sh
# RomM's slotted saves (RomM 5.4: versions per slot, the devices' sync records, negotiation), which BananaOS's save
# sync is built on, against tests/fake-romm.py:
#   the stand-in itself   tests/romm-conformance.py, the cases measured on a real RomM 5.4.1, against the stand-in (and
#                         against a real RomM when $ROMM_URL and $ROMM_TOKENS, a JSON of three client tokens, are set)
#   its faults            /test/fault's switches and /test/strict's tripwires, each doing what it says
#   other clients         tests/romm-peers.py, a Grout-style client and one writing as BananaOS's sync will, sharing
#                         the slots on a stand-in of their own
#   the real card         tests/sync-card.sh's helpers, a card laid out as a handheld's /data really is
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

# no traceback but those of the faults that asked the handler to fail: each traceback's last line, the exception, is
# what tells them apart
for f in "$OUT/fake-romm.log" "$OUT/fake-romm2.log"; do
    u=$(awk '/^Traceback/ { t = 1; next } t && !/^ / { t = 0; if ($0 !~ /^RuntimeError: a fault asked for on/) print }' "$f")
    [ -z "$u" ] || bad "the stand-in failed a request ($f): $u"
done

[ -s "$OUT/failures" ] || { echo "sync-slots: all checks passed"; exit 0; }
echo "sync-slots: $(wc -l < "$OUT/failures") checks failed ($OUT/failures)"
exit 1

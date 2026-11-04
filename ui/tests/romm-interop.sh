#!/bin/sh
# What a RomM 5.4 shared with other clients sees of BananaOS's save sync, as the save sync's design promises it, one
# case per row (Grout's side by tests/romm-peers.py's stand-in of it, or by the real Grout's own sync code): played
# here then on Grout, on Grout then here, both changed, the card's clock a day out either way, a core's fresh save, the
# emulator's data cleared, NextUI's "<rom>.gba.sav", a first pairing with a save of its own, a DraStic save with and
# without DeSmuME's footer, a DS save of no cartridge's size, DeSmuME's footer under a .sav name, an N64 file of a
# standalone emulator, many uploads past the server's cap, and a client sharing BananaOS's label that prunes its
# lineage to 10. Around every run of
# banana-sync the server's rows for the games in play are recorded: a run that removed one, or changed one in place,
# fails, and so does a version written on a card that is then nowhere, on a card, in a vault or on the server (before
# the last two cases, which are allowed to take the older ones).
#
#   romm-interop.sh                              against tests/fake-romm.py, with a library of its own
#   ROMM_URL=... ROMM_TOKENS=... romm-interop.sh against a real RomM: three client tokens of one user as JSON {"A", "B",
#                                                "C"} (or a file of it). It writes save rows into the first GBA games
#                                                of the server, and into a DS and an N64 game when it has them (the
#                                                cases of a system the server lacks are skipped): a server for tests
#   GROUT_SRC=<grout checkout> romm-interop.sh   either, with the real Grout: the checkout at the commit in tests/GROUT,
#                                                its sync built headless by tools/grout-peer.sh (Go and the SDL2
#                                                development packages)
#
# The version gate (a RomM before 5.4) is tests/sync.sh's.
HERE=$(cd "$(dirname "$0")/.." && pwd)
OUT=${OUT:-$HERE/tests/out}/romm-interop
rm -rf "$OUT"; mkdir -p "$OUT"
bad() { echo "FAIL romm-interop: $*" >&2; printf '%s\n' "$(printf '%s' "$*" | tr '\n' ' ')" >> "$OUT/failures"; }
skip() { echo "SKIP romm-interop: $*"; }
PIDS=""
cleanup() { for p in $PIDS; do kill "$p" 2>/dev/null; done; }
trap cleanup EXIT INT TERM
port() { python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1])'; }
waitport() { for i in $(seq 50); do curl -s "http://127.0.0.1:$1$2" >/dev/null && return 0; sleep 0.1; done; return 1; }
jget() { python3 -c "import json,sys; print(json.load(sys.stdin)$1)"; }
m() { md5sum < "$1" | cut -c1-32; }
. "$HERE/tests/sync-card.sh"
# ra <command> ...: tests/romm-api.py, a wrong answer from the server a failure (it says so on stderr, so not into
# what a $(...) takes)
ra() { python3 "$HERE/tests/romm-api.py" "$@" || bad "romm-api.py $1: a wrong answer (above)"; }

BIN=$OUT/banana-sync
${CXX:-g++} -std=c++17 -O2 -Wall -o "$BIN" "$HERE/src/sync.cpp" "$HERE/src/sync_core.cpp" "$HERE/src/sync_romm.cpp" \
    "$HERE/src/save_adapt.cpp" "$HERE/src/save_paths.cpp" "$HERE/src/save_vault.cpp" "$HERE/src/json.cpp" -ldl || { bad "banana-sync doesn't build"; exit 1; }
mkdir -p "$OUT/cores"; fakecore "$OUT/cores/gpsp_libretro.so" && fakecore "$OUT/cores/mupen64plus_next_libretro.so" || bad "the test cores can't be put in place"
export BANANA_SYSTEMS="$HERE/../system/rootfs/etc/banana/systems.conf" BANANA_LIBRETRO="$OUT/cores"

FAKE=0
if [ -n "$ROMM_URL" ] && [ -n "$ROMM_TOKENS" ]; then
    U=${ROMM_URL%/}; TA=$(ra token "$ROMM_TOKENS" A); TB=$(ra token "$ROMM_TOKENS" B); TC=$(ra token "$ROMM_TOKENS" C)
    CAP=50      # the versions RomM keeps of one lineage in a slot
    WHERE="the RomM at $U"
else
    L=$OUT/lib; mkdir -p "$L/gba" "$L/nds" "$L/n64"
    for i in 01 02 03 04 05 06 07 08 09; do printf 'gba rom interop %s' "$i" > "$L/gba/Game $i (USA).gba"; done
    head -c 4096 /dev/urandom > "$L/nds/Interop DS (USA).nds"; head -c 4096 /dev/urandom > "$L/n64/Interop 64 (USA).z64"
    P=$(port); FAKE=1; CAP=11     # (the stand-in's cap lowered, so a case can reach it)
    FAKE_ROMM_MAX_SAVES_PER_SLOT=$CAP python3 "$HERE/tests/fake-romm.py" "$P" "$L" 2> "$OUT/fake-romm.log" & PIDS="$PIDS $!"
    waitport "$P" /api/heartbeat || { bad "fake RomM didn't start: $(tail -3 "$OUT/fake-romm.log")"; exit 1; }
    U=http://127.0.0.1:$P
    newtok() { curl -s -XPOST "$U/test/token" -d '{}' | jget '["token"]'; }
    TA=$(newtok); TB=$(newtok); TC=$(newtok)
    # (the cards' tokens strict: what BananaOS must never send fails the run; the peers' are other clients)
    curl -s -XPOST "$U/test/strict" -d "{\"tokens\": [\"$TA\", \"$TB\"]}" >/dev/null
    WHERE="the stand-in"
fi
ra roms "$U" "$TC" gba > "$OUT/gba-all.tsv"; head -9 "$OUT/gba-all.tsv" > "$OUT/gba.tsv"
ra roms "$U" "$TC" nds > "$OUT/nds-all.tsv"; head -1 "$OUT/nds-all.tsv" > "$OUT/nds.tsv"
ra roms "$U" "$TC" n64 > "$OUT/n64-all.tsv"; head -1 "$OUT/n64-all.tsv" > "$OUT/n64.tsv"
[ "$(wc -l < "$OUT/gba.tsv")" -ge 9 ] || { bad "$WHERE has fewer than nine GBA games"; exit 1; }
cat "$OUT/gba.tsv" "$OUT/nds.tsv" "$OUT/n64.tsv" > "$OUT/all.tsv"
idof() { awk -F'\t' -v n="$1" '$2 == n { print $1 }' "$OUT/all.tsv"; }
game() { sed -n "${1}p" "$OUT/gba.tsv" | cut -f2; }
IDS=$(cut -f1 "$OUT/all.tsv" | tr '\n' ' ')

# a card paired with the server: a handheld's real layout, the server's own ROM files (so they match by content), its
# device registered at its first sync
card() {
    _c=$OUT/$1; realcard "$_c"
    while IFS='	' read -r _id _n; do
        _s=gba; grep -q "^$_id	" "$OUT/nds.tsv" && _s=nds; grep -q "^$_id	" "$OUT/n64.tsv" && _s=n64
        ra fetch "$U" "$2" "$_id" "$_n" "$_c/roms/$_s/$_n"
    done < "$OUT/all.tsv"
    printf '{"backend": "romm", "server": "%s", "token": "%s"}\n' "$U" "$2" > "$_c/banana/romsync.json"
}
# DS saves brought together by the real nds-saves on the card
printf '#!/bin/sh\nBANANA_ROOT=$BANANA_DATA_ROOT exec sh "%s" "$@"\n' "$HERE/../system/rootfs/usr/libexec/banana/nds-saves" > "$OUT/nds-saves"; chmod +x "$OUT/nds-saves"

# rows: every save row of the games in play
rows() { ra rows "$U" "$TC" $IDS; }
blankcheck() { "$BIN" blank "$1" "$(basename "$(dirname "$1")")" >/dev/null 2>&1; }
# sync <card>: one run of its saves, the rows around it compared: none gone (unless MAYGO=gone, past the server's
# cap), none changed in place. Each real save it finds is noted as a version written
VERS=$OUT/versions MAYGO=
sync() {
    _c=$OUT/$1
    find "$_c/saves" -type f \( -name '*.srm' -o -name '*.sav' \) | while read -r _f; do
        blankcheck "$_f" || m "$_f"; done >> "$VERS"
    rows > "$OUT/rows.before"
    BANANA_DATA_ROOT=$_c BANANA_NDS_SAVES=$OUT/nds-saves "$BIN" --root "$_c" --run "$_c/run" --status "$_c/run/sync.status" sync --saves-only > "$OUT/$1.log" 2>&1
    rows > "$OUT/rows.after"
    python3 "$HERE/tests/romm-api.py" compare "$OUT/rows.before" "$OUT/rows.after" "$OUT/rows.diff" $MAYGO || bad "$1's sync removed or replaced a row on the server: $(cat "$OUT/rows.diff")"
}
rv() { _n=$1; shift; "$BIN" --root "$OUT/$_n" --run "$OUT/$_n/run" "$@" 2>/dev/null; }
# grout: one sync on Grout's card, by tests/romm-peers.py's scripted Grout or, with GROUT_SRC (a Grout checkout at the
# commit in tests/GROUT), by the real Grout's own sync code, built headless by tools/grout-peer.sh
if [ -n "$GROUT_SRC" ]; then
    sh "$HERE/../tools/grout-peer.sh" "$GROUT_SRC" "$OUT/grout-peer" > "$OUT/grout-build.log" 2>&1 || { bad "the real Grout didn't build: $(tail -3 "$OUT/grout-build.log")"; exit 1; }
    gsync() { "$OUT/grout-peer" "$U" "$TC" "$OUT/grout"; }
    WHO="the real Grout ($(cut -d' ' -f3 "$HERE/tests/GROUT"))"
else
    skip "the real Grout (GROUT_SRC, a Grout checkout at the commit in tests/GROUT): Grout's side is the scripted one"
    gsync() { python3 "$HERE/tests/romm-peers.py" "$U" "$TC" grout "$OUT/grout" sync; }
    WHO="the scripted Grout"
fi
grout() { gsync > "$OUT/grout.log" 2>&1 || bad "Grout's run failed: $(tail -3 "$OUT/grout.log")"; }
peer() { python3 "$HERE/tests/romm-peers.py" "$U" "$TC" bananaos "$OUT/peer" "$@"; }
head_of() { ra head "$U" "$TC" "$@"; }
nrows() { ra count "$U" "$TC" "$@"; }
# vaulted <card> <save, as the card has it under its folder> <file>: the file's bytes kept in that card's vault for
# that save
vaulted() { ls "$OUT/$1/banana/save-vault/${2%.*}/" 2>/dev/null | grep -q -- "-$(m "$3" | cut -c1-8)\.${2##*.}$"; }
# pick <n>: the case's game: G its file, I its id, K its name, S the card's save, V that save under the card's folder;
# the game also put on Grout's card, as Grout looks for saves only for the ROMs it has
pick() { G=$(game "$1"); I=$(idof "$G"); K=${G%.*}; V=saves/gba/$K.srm; S=$OUT/a/$V; : > "$GR/$G"; }
LABEL=gpsp      # the card's GBA saves are written as their core's name: gpsp_libretro.so's
GS=$OUT/grout/Saves/GBA GR="$OUT/grout/Roms/Game Boy Advance (GBA)"
mkdir -p "$GS" "$GR"
# gplay <rom file>: played on Grout, which sends what changed since its last sync by the file's time (a second ahead,
# as the save comes in the same second as that sync here)
gplay() { head -c 8192 /dev/urandom > "$GS/$1.sav"; touch -d "@$(( $(date +%s) + 2 ))" "$GS/$1.sav"; }
# gkept <rom file> <file>: the file's bytes in Grout's .backup for that game, beside its saves, where it puts what a
# download replaces
gkept() { for _f in "$GS/.backup/$1 ["*; do [ -f "$_f" ] && cmp -s "$_f" "$2" && return 0; done; return 1; }
card a "$TA"
# base: each case starts from a version the card, Grout and the server all hold, whatever a server used before has as
# the game's head: a save put up by another device, which the card and Grout then take, each over what it holds
# (nothing yet, or a version agreed earlier in the run). Nothing is deleted on either to make room: a save deleted on
# the card is a lost save, held by design
base() {
    head -c 8192 /dev/urandom > "$OUT/base.srm"
    peer join "$I" >/dev/null; peer upload "$I" "$OUT/base.srm" >/dev/null; sync a; grout
    cmp -s "$S" "$OUT/base.srm" || bad "$G: the case's start not taken by the card: $(cat "$OUT/a.log")"
    cmp -s "$GS/$G.sav" "$OUT/base.srm" || bad "$G: the case's start not taken by Grout: $(cat "$OUT/grout.log")"
}

# ---- played here, then on Grout: Grout takes the card's save (BananaOS's the newest head), its own kept in .backup
pick 1; base; head -c 8192 /dev/urandom > "$S"; sync a; grout
cmp -s "$GS/$G.sav" "$S" && gkept "$G" "$OUT/base.srm" || bad "played here then on Grout: Grout didn't get it, or didn't keep its own: $(cat "$OUT/a.log" "$OUT/grout.log")"

# ---- played on Grout, then here: the card takes Grout's (NextUI's "<rom>.gba.sav"), the save it replaces kept
pick 2; base; head -c 8192 /dev/urandom > "$S"; sync a; grout
cp "$S" "$OUT/g2-was.srm"; gplay "$G"; grout; sync a
cmp -s "$S" "$GS/$G.sav" && vaulted a "$V" "$OUT/g2-was.srm" || bad "played on Grout then here: not taken, or the card's not kept: $(cat "$OUT/a.log")"

# ---- both changed: held, the card's as it is, both kept (the server's in the vault, for a choice of it)
pick 3; base; head -c 8192 /dev/urandom > "$S"; sync a; grout
head -c 8192 /dev/urandom > "$S"; cp "$S" "$OUT/g3-card.srm"; gplay "$G"; grout; sync a
cmp -s "$S" "$OUT/g3-card.srm" && grep -q "conflict, both kept: $V" "$OUT/a.log" && [ "$(head_of "$I")" = "$(m "$GS/$G.sav")" ] && vaulted a "$V" "$GS/$G.sav" ||
    bad "both changed: not held as it is, or the server's not kept: $(cat "$OUT/a.log")"

# ---- the card's clock a day out: decisions by content, whatever the files' times say
pick 4; base
gplay "$G"; grout; touch -d '+1 day' "$S"; sync a
cmp -s "$S" "$GS/$G.sav" || bad "a card a day ahead: the server's newer save not taken: $(cat "$OUT/a.log")"
head -c 8192 /dev/urandom > "$S"; touch -d '-1 day' "$S"; sync a
[ "$(head_of "$I")" = "$(m "$S")" ] || bad "a card a day behind: its new save not sent: $(cat "$OUT/a.log")"

# ---- a core's fresh save: never sent, the server's head as it was; one blanked since it synced is held, not refilled
pick 8; rm -f "$S"; blank "$S" 131072 ff; n=$(nrows "$I"); h=$(head_of "$I"); sync a
[ "$(nrows "$I")" = "$n" ] && [ "$(head_of "$I")" = "$h" ] || bad "a fresh gpSP save sent: $(cat "$OUT/a.log")"
pick 5; base; h=$(head_of "$I"); blank "$S" 131072 ff; n=$(nrows "$I"); sync a
[ "$(nrows "$I")" = "$n" ] && [ "$(head_of "$I")" = "$h" ] && grep -q "blank here since the last sync" "$OUT/a.log" ||
    bad "a save blanked since it synced: sent, or not held: $(cat "$OUT/a.log")"

# ---- the emulator's data cleared: held, nothing up or down by itself
pick 6; base; rm "$S"; n=$(nrows "$I"); sync a
[ ! -e "$S" ] && [ "$(nrows "$I")" = "$n" ] && rv a saves | grep -q "$V	missing here since the last sync" ||
    bad "the data cleared: refilled, sent, or not held: $(cat "$OUT/a.log")"

# ---- a first pairing with a save of its own and another head on the server: held, nothing sent over it, the
# server's kept in the vault
card b "$TB"; pick 1; SB=$OUT/b/$V; head -c 8192 /dev/urandom > "$SB"; cp "$SB" "$OUT/b1.srm"
h=$(head_of "$I"); cp "$GS/$G.sav" "$OUT/b-server.srm"; sync b
cmp -s "$SB" "$OUT/b1.srm" && [ "$(head_of "$I")" = "$h" ] && grep -q "conflict, both kept: $V" "$OUT/b.log" && vaulted b "$V" "$OUT/b-server.srm" ||
    bad "a first pairing over another head: not held, or the server's not kept: $(cat "$OUT/b.log")"

# ---- the DS: DraStic's upload with DeSmuME's footer and without, and the footer under a .sav name, each taken as the
# raw save, DraStic's own put in step; one of no cartridge's size held
if [ -s "$OUT/nds.tsv" ]; then
    D=$(cut -f2 "$OUT/nds.tsv"); DI=$(idof "$D"); DK=${D%.*}; DS=$OUT/a/saves/nds/$DK.sav; DD=$OUT/a/drastic/backup/$DK.dsv; PU=$OUT/peer-up
    # dsup <file>: put up by the peer under that file's name, over the head it has seen; the card's sync
    dsup() { peer join "$DI" >/dev/null; peer upload "$DI" "$1" > /dev/null; sync a; }
    mkdir -p "$PU"; head -c 8192 /dev/urandom > "$OUT/ds1.sav"; dsv "$OUT/ds1.sav" "$PU/$DK.dsv"; dsup "$PU/$DK.dsv"
    cmp -s "$DS" "$OUT/ds1.sav" && cmp -s "$DD" "$OUT/ds1.sav" || bad "DraStic's .dsv with its footer: not taken as the raw save: $(cat "$OUT/a.log")"
    head -c 8192 /dev/urandom > "$PU/$DK.sav"; dsup "$PU/$DK.sav"
    cmp -s "$DS" "$PU/$DK.sav" && cmp -s "$DD" "$PU/$DK.sav" || bad "a raw DS save: not taken, or DraStic's not in step: $(cat "$OUT/a.log")"
    head -c 8192 /dev/urandom > "$OUT/ds3.sav"; dsv "$OUT/ds3.sav" "$PU/$DK.sav"; dsup "$PU/$DK.sav"
    cmp -s "$DS" "$OUT/ds3.sav" && cmp -s "$DD" "$OUT/ds3.sav" || bad "DeSmuME's footer under a .sav name: not taken as the raw save: $(cat "$OUT/a.log")"
    head -c 3000 /dev/urandom > "$PU/$DK.sav"; cp "$DS" "$OUT/ds-was.sav"; dsup "$PU/$DK.sav"
    cmp -s "$DS" "$OUT/ds-was.sav" && grep -q "not taken yet" "$OUT/a.log" || bad "a DS save of no cartridge's size: taken, or not held: $(cat "$OUT/a.log")"
else
    skip "the DS cases: $WHERE has no DS game"
fi

# ---- an N64 save of a standalone emulator (an .eep): held, the card's left as it is
if [ -s "$OUT/n64.tsv" ]; then
    N=$(cut -f2 "$OUT/n64.tsv"); NI=$(idof "$N"); mkdir -p "$OUT/peer-up"; head -c 512 /dev/urandom > "$OUT/peer-up/${N%.*}.eep"
    # (what the card has for N64 before, which a server used before may have given it: the .eep changes none of it)
    nsum() { find "$OUT/a/saves" -maxdepth 1 -type f -name "${N%.*}.*" -exec md5sum {} + 2>/dev/null | sort; }
    nsum > "$OUT/n64.card"
    peer join "$NI" >/dev/null; peer upload "$NI" "$OUT/peer-up/${N%.*}.eep" > /dev/null; sync a
    grep -q "not taken yet, a format this handheld doesn't read: ${N%.*}" "$OUT/a.log" && nsum | cmp -s - "$OUT/n64.card" ||
        bad "an N64 .eep: taken, or not held: $(cat "$OUT/a.log")"
else
    skip "the N64 case: $WHERE has no N64 game"
fi

# ---- every version written on a card is still somewhere
find "$OUT/a" "$OUT/b" "$OUT/grout" -type f \( -name '*.srm' -o -name '*.sav' -o -name '*.dsv' \) -exec md5sum {} + | cut -c1-32 > "$OUT/somewhere"
rows | awk '{ print $2 }' >> "$OUT/somewhere"
sort -u "$VERS" | while read -r v; do grep -qx "$v" "$OUT/somewhere" || echo "$v"; done > "$OUT/lost"
[ -s "$OUT/lost" ] && bad "versions written on a card now nowhere: $(cat "$OUT/lost")"

# ---- many uploads from the card: the server keeps its cap of them, the newest the head
MAYGO=gone
# (the lineage counted by the label as written and without regard to case: which one the server's cap counts by is its
# database's collation, MariaDB's default holding gpSP and gpsp as one, and a server an older peer wrote to may hold
# rows of both. Either count kept to the cap passes)
lineage() { ra rows "$U" "$TC" "$1" | awk -v l="$LABEL" -v ci="$2" '$4 == "autosave" && (ci ? tolower($5) == tolower(l) : $5 == l)' | wc -l | tr -d ' '; }
capped() { [ $(( $1 + 12 < CAP ? $1 + 12 : CAP )) = "$2" ]; }
pick 9; base; n=$(lineage "$I" 0); nc=$(lineage "$I" 1)
for i in $(seq 12); do head -c 8192 /dev/urandom > "$S"; sync a; done
{ capped "$n" "$(lineage "$I" 0)" || capped "$nc" "$(lineage "$I" 1)"; } && [ "$(head_of "$I")" = "$(m "$S")" ] ||
    bad "twelve uploads: $(lineage "$I" 0) of the card's versions kept ($(lineage "$I" 1) of any case), from $n ($nc), or the last not the head: $(cat "$OUT/a.log")"

# ---- a client sharing BananaOS's label prunes its lineage to 10: older BananaOS versions go, never the head or the
# card's vault, and the card takes the new head as any other
pick 7; base
for i in $(seq 12); do head -c 8192 /dev/urandom > "$S"; sync a; done
n=$(nrows "$I" "$LABEL"); [ "$n" -gt 10 ] 2>/dev/null || bad "before the pruning: $n of the card's versions on the server, not more than 10"
find "$OUT/a/banana/save-vault" -type f | sort > "$OUT/vault.before"
head -c 8192 /dev/urandom > "$OUT/pruner.srm"; peer join "$I" >/dev/null
code=$(peer upload "$I" "$OUT/pruner.srm" prune "$LABEL"); [ "$code" = 200 ] || [ "$code" = 201 ] || bad "the pruning upload refused: $code"
n=$(nrows "$I" "$LABEL"); [ "$n" -le 10 ] 2>/dev/null || bad "the pruning upload: $n versions left of the lineage, not 10 or fewer"
cp "$S" "$OUT/g7-was.srm"; sync a
cmp -s "$S" "$OUT/pruner.srm" && [ "$(head_of "$I")" = "$(m "$OUT/pruner.srm")" ] && vaulted a "$V" "$OUT/g7-was.srm" ||
    bad "a pruned lineage: the new head not taken, or the card's not kept: $(cat "$OUT/a.log")"
find "$OUT/a/banana/save-vault" -type f | sort > "$OUT/vault.after"
[ -z "$(comm -23 "$OUT/vault.before" "$OUT/vault.after")" ] || bad "the vault lost a copy: $(comm -23 "$OUT/vault.before" "$OUT/vault.after")"

if [ $FAKE = 1 ]; then
    [ "$(curl -s "$U/test/violations")" = "[]" ] || bad "the cards sent what they never may: $(curl -s "$U/test/violations")"
fi
[ -s "$OUT/failures" ] && { echo "romm-interop: $(wc -l < "$OUT/failures") checks failed ($OUT/failures)"; exit 1; }
echo "romm-interop: all passed against $WHERE, with $WHO"

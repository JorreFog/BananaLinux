#!/bin/sh
# The DS launchers' file work against a made-up card, no emulator: run-drastic's per-game drastic.cfg lines (#117)
# and its move of fast forward off R3 (#113), taken from run-drastic itself (its functions, not a copy); nds-saves on
# a made-up data partition (BANANA_ROOT): an empty save never goes over a real one (#96).
#   sh system/tests/launchers-test.sh
cd "$(dirname "$0")/../.." || exit 1
R=$PWD L=$PWD/system/rootfs/usr/libexec/banana
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
passed=0 failed=0
check() { if [ "$1" = "$2" ]; then passed=$((passed + 1)); echo "  ok: $3"; else failed=$((failed + 1)); echo "  FAIL: $3 (got '$1', want '$2')"; fi; }
# fn <file> <name>...: those shell functions out of a launcher, defined here
fn() { f=$1; shift; for n; do eval "$(sed -n "/^$n() {/,/^}/p" "$f")"; done; }

echo "run-drastic: a game's own 3D resolution (hires_3d) and Threaded 3D"
fn "$L/run-drastic" cfg_game cfg_back hires_of
check "$(hires_of 1) $(hires_of 2) $(hires_of 3) $(hires_of 4) [$(hires_of '')] [$(hires_of 5)]" "0 1 1 1 [] []" "1x is 0, 2x 3x 4x are 1, nothing else"
cd "$T" && mkdir config || exit 1
hires() { sed -n '/^hires_3d = /{s///p;q}' config/drastic.cfg; }
# run <hires_3d before> <game's res>: what run-drastic does around SuperDrastic; prints the line during the run, after, and whether it would exec
run() {
    printf 'threaded_3d = 0\nhires_3d = %s\n' "$1" > config/drastic.cfg
    CH=; cfg_game hires_3d "$(hires_of "$2")" && CH=1
    during=$(hires); marker=$( [ -f config/.hires_3d.settings ] && cat config/.hires_3d.settings)
    cfg_back hires_3d
    echo "${CH:-exec} during=$during marker=${marker:-none} after=$(hires) left=$(ls -A config | grep -c settings)"
}
check "$(run 0 '')" "exec during=0 marker=none after=0 left=0" "no line for the game: Settings' 0, exec"
check "$(run 1 1)" "1 during=0 marker=1 after=1 left=0" "1x over Settings' 2x: 0 for the run, 1 back"
check "$(run 0 1)" "exec during=0 marker=none after=0 left=0" "1x over Settings' 1x: no change, no marker"
check "$(run 0 2)" "1 during=1 marker=0 after=0 left=0" "2x over 1x: 1 for the run, 0 back"
check "$(run 0 4)" "1 during=1 marker=0 after=0 left=0" "4x is 2x (never hires_3d = 4)"
check "$(run 1 3)" "exec during=1 marker=none after=1 left=0" "3x over Settings' 2x: no change"
# both lines: Threaded 3D and the 3D resolution together, both back
printf 'threaded_3d = 1\nhires_3d = 0\n' > config/drastic.cfg
cfg_game threaded_3d 0; cfg_game hires_3d 1
check "$(tr '\n' ' ' < config/drastic.cfg)" "threaded_3d = 0 hires_3d = 1 " "both of the game's lines in for the run"
cfg_back threaded_3d; cfg_back hires_3d
check "$(tr '\n' ' ' < config/drastic.cfg)[$(ls -A config | grep -c settings)]" "threaded_3d = 1 hires_3d = 0 [0]" "both of Settings' back, no marker left"
# a power cut mid-run: the next start puts Settings' back from the marker; an empty one is dropped
printf 'threaded_3d = 0\nhires_3d = 0\n' > config/drastic.cfg; echo 1 > config/.hires_3d.settings
cfg_back hires_3d
check "$(hires) $(ls -A config | grep -c settings)" "1 0" "power cut: Settings' 1 back at the next start, marker gone"
: > config/.hires_3d.settings
cfg_back hires_3d
check "$(hires) $(ls -A config | grep -c settings)" "1 0" "an empty marker is dropped, the line left alone"
cfg_game hires_3d 4
check "$(hires) $(ls -A config | grep -c settings)" "1 0" "a value other than 0 or 1 never goes in"

echo "run-drastic: fast forward off R3 (#113), save on L2, load from the menu (#77)"
fn "$L/run-drastic" ff_on_r2
IMG=$R/buildroot/board/rgds/drastic.cfg
keys() { grep -E "^controls_$1\[CONTROL_INDEX_(FAST_FORWARD|SAVE_STATE|LOAD_STATE)\]" "${2:-config/drastic.cfg}" | sed 's/.*_INDEX_//; s/\] = /=/' | tr '\n' ' '; }
check "$(keys a "$IMG")" "SAVE_STATE=1030 LOAD_STATE=65535 FAST_FORWARD=1031 " "the image's controls_a: save L2, load none, fast forward R2"
check "$(keys b "$IMG")" "SAVE_STATE=1030 LOAD_STATE=65535 FAST_FORWARD=1031 " "the image's controls_b: the same"
check "$(grep -c '= 1036$' "$IMG")" "0" "nothing on R3 in the image"
# a card from before: the image's file with the old three
sed -e 's/^\(controls_[ab]\[CONTROL_INDEX_SAVE_STATE\]\) = .*/\1 = 1031/' -e 's/^\(controls_[ab]\[CONTROL_INDEX_LOAD_STATE\]\) = .*/\1 = 1030/' \
    -e 's/^\(controls_[ab]\[CONTROL_INDEX_FAST_FORWARD\]\) = .*/\1 = 1036/' "$IMG" > config/drastic.cfg
ff_on_r2
check "$(cmp -s config/drastic.cfg "$IMG" && echo same)" "same" "an old card's controls become the image's"
cp config/drastic.cfg config/once; ff_on_r2
check "$(cmp -s config/drastic.cfg config/once && echo same)" "same" "twice changes nothing"
# a player's own setup in set b (load moved to L, 1028): set b left alone, set a moved
sed -e 's/^\(controls_[ab]\[CONTROL_INDEX_SAVE_STATE\]\) = .*/\1 = 1031/' -e 's/^\(controls_[ab]\[CONTROL_INDEX_FAST_FORWARD\]\) = .*/\1 = 1036/' \
    -e 's/^\(controls_a\[CONTROL_INDEX_LOAD_STATE\]\) = .*/\1 = 1030/' -e 's/^\(controls_b\[CONTROL_INDEX_LOAD_STATE\]\) = .*/\1 = 1028/' "$IMG" > config/drastic.cfg
ff_on_r2
check "$(keys a)" "SAVE_STATE=1030 LOAD_STATE=65535 FAST_FORWARD=1031 " "set a, as it was: moved"
check "$(keys b)" "SAVE_STATE=1031 LOAD_STATE=1028 FAST_FORWARD=1036 " "set b, changed by the player: left alone"
rm -f config/drastic.cfg; ff_on_r2
check "$(ls config | grep -c drastic.cfg)" "0" "no drastic.cfg yet: nothing made"
cd - >/dev/null || exit 1

echo "nds-saves: an empty save never goes over one with something in it (#96)"
D=$T/data
# saves <dsv bytes|-> <sav bytes|-> <newer: dsv|sav>: a made-up card with those two files ('-': none), then nds-saves
saves() {
    rm -rf "$D"; mkdir -p "$D/drastic/backup" "$D/saves/nds"
    [ "$1" = - ] || head -c "$1" /dev/zero | tr '\0' d > "$D/drastic/backup/Game.dsv"
    [ "$2" = - ] || head -c "$2" /dev/zero | tr '\0' s > "$D/saves/nds/Game.sav"
    for f in "$D/drastic/backup/Game.dsv" "$D/saves/nds/Game.sav"; do [ -f "$f" ] && touch -d '2026-01-01 10:00' "$f"; done
    case $3 in dsv) touch -d '2026-01-01 11:00' "$D/drastic/backup/Game.dsv" ;; sav) touch -d '2026-01-01 11:00' "$D/saves/nds/Game.sav" ;; esac
    BANANA_ROOT=$D sh "$L/nds-saves" "/roms/nds/Game.nds" > "$T/log"
}
# what each holds now: <bytes><its letter>
held() { for f in "$D/drastic/backup/Game.dsv" "$D/saves/nds/Game.sav"; do [ -f "$f" ] && printf '%s%s ' "$(wc -c < "$f" | tr -d ' ')" "$(head -c 1 "$f")" || printf -- '- '; done; }
saves 8192 0 sav
check "$(held)" "8192d 8192d " "an empty .sav newer than an 8 KB .dsv: the .dsv kept and copied over it"
check "$(grep -c 'empty (0 bytes)' "$T/log")" "1" "...and said so"
saves 8192 100 sav
check "$(held)" "8192d 8192d " "a 100-byte .sav (no save) newer: the same"
saves 0 8192 dsv
check "$(held)" "8192s 8192s " "an empty .dsv newer than an 8 KB .sav: the .sav kept and copied over it"
saves 8192 512 sav
check "$(held)" "512s 512s " "a 512-byte .sav newer (a 4 Kbit EEPROM): taken, as before"
saves 512 8192 dsv
check "$(held)" "512d 512d " "a newer .dsv that is a save: taken, as before"
saves 8192 - x
check "$(held)" "8192d 8192d " "no .sav yet: made from the .dsv"
saves - 8192 x
check "$(held)" "8192s 8192s " "no .dsv yet: made from the .sav"
saves 8192 0 sav; saves_again=$(held)
BANANA_ROOT=$D sh "$L/nds-saves" "/roms/nds/Game.nds" > "$T/log"
check "$(held) $(wc -c < "$T/log" | tr -d ' ')" "$saves_again 0" "a second run: nothing more to do"
# a ROCKNIX save beside the game (#25): an empty one is not taken
rm -rf "$D"; mkdir -p "$T/roms"; : > "$T/roms/Game.dsv"
BANANA_ROOT=$D sh "$L/nds-saves" "$T/roms/Game.nds" > "$T/log"
check "$(held)" "- - " "an empty ROCKNIX .dsv beside the game is not taken"
head -c 8192 /dev/zero | tr '\0' r > "$T/roms/Game.dsv"
BANANA_ROOT=$D sh "$L/nds-saves" "$T/roms/Game.nds" > "$T/log"
check "$(held)" "8192r 8192r " "one with a save in it is"

echo "launchers-test: $passed passed, $failed failed"
[ $failed = 0 ]

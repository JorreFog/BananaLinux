#!/bin/sh
# The DS launchers' file work against a made-up card, no emulator: run-drastic's per-game drastic.cfg lines (#117),
# taken from run-drastic itself (its functions, not a copy).
#   sh system/tests/launchers-test.sh
cd "$(dirname "$0")/../.." || exit 1
L=$PWD/system/rootfs/usr/libexec/banana
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
cd - >/dev/null || exit 1

echo "launchers-test: $passed passed, $failed failed"
[ $failed = 0 ]

#!/bin/sh
# Activity's history on a made-up card (no panels: --smoke draws into files): the first run only seeds the snapshot,
# and a game played for the first time after it has its sessions recorded (it was seeded as another "first run", and
# its first plays never reached the history).
#   sh store/apps/activity/tests/sync-test.sh        (builds the program for this computer with ${CXX:-g++})
HERE=$(cd "$(dirname "$0")/.." && pwd)
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
${CXX:-g++} -O2 -std=c++17 -Wno-format-truncation -o "$T/activity" "$HERE/banana-activity.cpp" || { echo "FAIL activity: doesn't build"; exit 1; }
fail=0
bad() { echo "FAIL activity: $*"; fail=1; }
run() { "$T/activity" --root "$1" --smoke "$T/shot" >/dev/null 2>&1 || bad "exited $? on $1"; }
sessions() { _n=$(grep -c "$2" "$1/banana/activity/sessions.tsv" 2>/dev/null); echo "${_n:-0}"; }    # (0 with no file yet)
now=$(date +%s)

# a card with games played before Activity was ever opened
C=$T/card; mkdir -p "$C/banana"
printf 'nds/Old Game.nds\t5\t9000\t%s\n' $((now - 86400)) > "$C/banana/playstats.tsv"
run "$C"
[ "$(sessions "$C" 'Old Game')" = 0 ] || bad "the first run invented a history for a game played before it"
grep -q 'first run' "$C/banana/activity/activity.log" || bad "the first run isn't in the log"
# the old game played once more, and a new one for the first time (twice)
printf 'nds/Old Game.nds\t6\t10800\t%s\ngba/New Game.gba\t2\t3600\t%s\n' $((now - 3600)) $((now - 600)) > "$C/banana/playstats.tsv"
run "$C"
[ "$(sessions "$C" 'Old Game')" = 1 ] || bad "a game played once more: $(sessions "$C" 'Old Game') sessions, not 1"
[ "$(sessions "$C" 'New Game')" = 2 ] || bad "a game first played after the first run: $(sessions "$C" 'New Game') sessions, not its 2"
[ "$(grep -c 'first run' "$C/banana/activity/activity.log")" = 1 ] || bad "a later run was logged as a first run again"
# nothing new: nothing recorded again
run "$C"
[ "$(sessions "$C" 'New Game')" = 2 ] && [ "$(sessions "$C" 'Old Game')" = 1 ] || bad "a run with nothing new recorded sessions again"

# a card with nothing played yet when Activity is first opened: its first game's first play counts
E=$T/empty; mkdir -p "$E/banana"; : > "$E/banana/playstats.tsv"
run "$E"
printf 'gba/First Ever.gba\t1\t900\t%s\n' $((now - 300)) > "$E/banana/playstats.tsv"
run "$E"
[ "$(sessions "$E" 'First Ever')" = 1 ] || bad "the first game on an empty card: $(sessions "$E" 'First Ever') sessions, not 1"

[ $fail = 0 ] && echo "activity sync-test: all passed"
exit $fail

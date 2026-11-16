#!/bin/sh
# Randomizer's own checks, with no handheld and no Pillow: the host build is made from the
# same single source the image builds, on a made-up card, and its CLI, library scan, cover
# decode, DS banner icon, three choices and launch hand-off are exercised.
#   sh store/apps/randomizer/tests/run.sh
set -u
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
APP=$(CDPATH= cd -- "$HERE/.." && pwd)
REPO=$(CDPATH= cd -- "$APP/../../.." && pwd)
CXX=${CXX:-c++}
TMP=$(mktemp -d "${TMPDIR:-/tmp}/randomizer-test.XXXXXX") || exit 1
trap 'rm -rf "$TMP"' EXIT INT TERM

PASS=0
FAIL=0
check() { # <what> <command...>
	what=$1
	shift
	if "$@" >/dev/null 2>&1; then
		PASS=$((PASS + 1))
	else
		FAIL=$((FAIL + 1))
		printf 'FAIL: %s\n' "$what"
	fi
}
bad() { FAIL=$((FAIL + 1)); printf 'FAIL: %s\n' "$1"; }
drawn() { # <ppm>: more than 16 different byte values after the header (a blank screen has 1)
	python3 -c 'import sys; b=open(sys.argv[1],"rb").read(); p=b.split(b"\n",3)[3]; sys.exit(0 if len(set(p)) > 16 else 1)' "$1"
}
same_options() { # <ppm>: the three option plates' backgrounds are the same colour (they must all be C_PANEL)
	python3 -c '
import sys
b = open(sys.argv[1], "rb").read()
head = b.split(b"\n", 3)
w, h = map(int, head[1].split())
p = head[3]
s = w / 640.0
c = []
for top in (154, 230, 306):
    x, y = int(550 * s), int((top + 30) * s)
    c.append(p[(y * w + x) * 3:(y * w + x) * 3 + 3])
sys.exit(0 if len(set(c)) == 1 else 1)
' "$1"
}

if ! command -v "$CXX" >/dev/null 2>&1; then
	printf 'randomizer-test: no C++ compiler (%s): skipped\n' "$CXX"
	exit 0
fi
command -v python3 >/dev/null 2>&1 || { printf 'randomizer-test: no python3: skipped\n'; exit 0; }

if ! "$CXX" -O1 -std=c++17 -Wall -Wno-unused-function -o "$TMP/randomizer" "$APP/banana-randomizer.cpp" 2>"$TMP/build.log"; then
	sed 's/^/  /' "$TMP/build.log" | head -n 30
	printf 'FAIL: the host build\n'
	exit 1
fi
python3 "$HERE/make-card.py" "$TMP/card" >/dev/null || { printf 'FAIL: the card\n'; exit 1; }
mkdir -p "$TMP/empty"

R="$TMP/randomizer"
COMMON="--root $TMP/card --systems $TMP/card/systems.conf --roms $TMP/card/roms --assets $REPO/ui/assets/rnds"

# 1. the library: gamelist names, spaces in paths, and the three things that must not be picks
"$R" $COMMON --list >"$TMP/list.txt" 2>"$TMP/list.err" || bad "the list exits 0"
check "the GBA game is listed" grep -q "Alpha Quest (USA)" "$TMP/list.txt"
check "the NDS game uses gamelist.xml's name" grep -q "Beta Space Deluxe" "$TMP/list.txt"
check "a built-in app's folder is not a game" sh -c "! grep -q Files '$TMP/list.txt'"
check "the Store's apps folder is not a game" sh -c "! grep -q Apps '$TMP/list.txt'"
check "a hidden system is not a game" sh -c "! grep -q Gamma '$TMP/list.txt'"
check "a shelf.hide system is not a game" sh -c "! grep -q Delta '$TMP/list.txt'"
check "an .app entry is not a game" sh -c "! grep -q Widget '$TMP/list.txt'"
[ "$(wc -l < "$TMP/list.txt")" -eq 2 ] || bad "exactly two games in the test library"

# 2. the two screens at both panel sizes; PPM header and a non-blank picture
for size in 640x480 1024x768; do
	"$R" $COMMON --smoke "$TMP/s" --size "$size" --seed 1 >/dev/null 2>&1 || bad "smoke at $size"
	check "the top screen is a $size PPM" sh -c "head -c 2 '$TMP/s-top.ppm' | grep -q P6"
	check "the top screen is $size wide" sh -c "sed -n 2p '$TMP/s-top.ppm' | grep -q '^${size%x*} '"
	check "the top screen is drawn" drawn "$TMP/s-top.ppm"
	check "the bottom screen is drawn" drawn "$TMP/s-bottom.ppm"
	check "all three choices are the same colour at $size" same_options "$TMP/s-bottom.ppm"
done

# 3. start: the exact path (with spaces and parentheses) goes to the launch file
rm -f "$TMP/launch"
"$R" $COMMON --smoke "$TMP/s" --pick 0 --press a --launch-out "$TMP/launch" >/dev/null 2>&1 || bad "start exits 0"
check "start writes the GBA game's whole path" sh -c "grep -qxF '$TMP/card/roms/gba/Alpha Quest (USA).gba' '$TMP/launch'"

# 4. touch on START GAME does the same (the bottom screen's row is at y 154..218)
rm -f "$TMP/launch2"
"$R" $COMMON --smoke "$TMP/s" --pick 1 --touch 300,180 --launch-out "$TMP/launch2" >/dev/null 2>&1 || bad "touch exits 0"
check "a touch on START GAME writes the NDS game's path" sh -c "grep -qxF '$TMP/card/roms/nds/Beta Space.nds' '$TMP/launch2'"

# 5. X picks another game (the debug line names both picks)
"$R" $COMMON --smoke "$TMP/s" --seed 3 --input-debug --press x >/dev/null 2>"$TMP/picks.txt" || bad "reroll exits 0"
check "reroll draws two different picks" sh -c "[ \$(grep -c 'pick ' '$TMP/picks.txt') -ge 2 ]"
check "reroll does not repeat the first game" sh -c "[ \$(grep 'pick ' '$TMP/picks.txt' | sed 's/.*pick [0-9]*\\/[0-9]*: //' | sort -u | wc -l) -eq 2 ]"

# 6. the DS cartridges setting: the same DS game drawn as box art and as its own icon
"$R" $COMMON --smoke "$TMP/box" --pick 1 --seed 1 --setting box >/dev/null 2>&1 || bad "box art mode"
"$R" $COMMON --smoke "$TMP/icon" --pick 1 --seed 1 --setting icon >/dev/null 2>&1 || bad "game icon mode"
check "box art and the game icon are different pictures" sh -c "! cmp -s '$TMP/box-top.ppm' '$TMP/icon-top.ppm'"

# 7. dark and light are the menu's two palettes
"$R" $COMMON --smoke "$TMP/dark" --pick 1 --seed 1 --theme dark >/dev/null 2>&1 || bad "dark theme"
"$R" $COMMON --smoke "$TMP/light" --pick 1 --seed 1 --theme light >/dev/null 2>&1 || bad "light theme"
check "dark and light are different pictures" sh -c "! cmp -s '$TMP/dark-top.ppm' '$TMP/light-top.ppm'"
check "all three choices are the same colour in light too" same_options "$TMP/light-bottom.ppm"

# 7. an empty library: both screens still render, and the list is empty
"$R" --root "$TMP/card" --systems "$TMP/card/systems.conf" --roms "$TMP/empty" --assets "$REPO/ui/assets/rnds" \
	--smoke "$TMP/empty-shot" >/dev/null 2>&1 || bad "an empty library still renders"
check "an empty library draws its invitation" drawn "$TMP/empty-shot-top.ppm"

# 8. no systems list at all: the same quiet empty state, not a crash
"$R" --root "$TMP/card" --systems "$TMP/nope.conf" --roms "$TMP/card/roms" --assets "$REPO/ui/assets/rnds" \
	--smoke "$TMP/none-shot" >/dev/null 2>&1 || bad "a missing systems list still renders"

printf 'randomizer-test: %d passed, %d failed\n' "$PASS" "$FAIL"
[ "$FAIL" -eq 0 ]

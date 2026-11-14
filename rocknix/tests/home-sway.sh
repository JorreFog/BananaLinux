#!/bin/sh
# The Thor button under sway, as on ROCKNIX: banana-ui (SDL=1) as the frontend on two headless outputs the Thor's size,
# a stand-in game (sway-game.c: a frame number drawn into its picture and written to a file every 20 ms), and the
# button pressed through a test pipe of evdev events (BANANA_INPUT_FIFO: no /dev/uinput needed). Two games:
#   both:  a two-screen game, a window on each output (as melonDS or BananaDS), the menu's windows both closed; started
#          as ROCKNIX's runemu.sh starts one, in a control group of the system's (a scope of games.slice: here a
#          made-up slice, BANANA_GAME_CGROUP as hw.conf's HW_GAME_CGROUP) and in a session of its own, so only that
#          group's freezer stops it
#   panel: a RetroArch game through run-rocknix on the top output, the menu's bottom window kept for the game panel;
#          in the menu's own group for the game (gameCgroup), or stopped by signals without one
# For each: a tap brings the menu up on both outputs over the frozen game (its frame number stops: the control group's
# freezer, else signals); a tap again puts the game back, which goes on from the frame it was stopped at; held for
# three seconds, the quick menu comes up over the bottom output while the game goes on on the top one (its frame number
# keeps going), and the next press drops it. Screenshots of each step in $OUT/home-sway-<game>-<step>-{top,bottom}.png.
# Needs sway, swaymsg, grim, convert, a C compiler with SDL2, a banana-ui built with SDL=1, and root (the menu's
# /run/banana); skipped otherwise.
#   rocknix/tests/home-sway.sh
HERE=$(cd "$(dirname "$0")/../.." && pwd)
UI=${BANANA_UI:-$HERE/ui/banana-ui}
OUT=${OUT:-$HERE/ui/tests/out}
for t in sway swaymsg grim convert cc sdl2-config; do command -v $t >/dev/null || { echo "home sway test skipped (no $t)"; exit 0; }; done
ldd "$UI" 2>/dev/null | grep -q libSDL2 || { echo "home sway test skipped (banana-ui without SDL=1)"; exit 0; }
[ "$(id -u)" = 0 ] || { echo "home sway test skipped (not root: /run/banana)"; exit 0; }
mkdir -p "$OUT" /run/banana
L=$HERE/system/rootfs/usr/libexec/banana
# (in /tmp, not a long path: sway's socket's path must fit in 108 bytes)
RT=$(mktemp -d); chmod 700 "$RT"
fail=0
bad() { echo "FAIL home sway test: $*"; fail=1; }
cc -O2 -o "$RT/sway-game" "$HERE/rocknix/tests/sway-game.c" $(sdl2-config --cflags --libs) 2>"$RT/cc.log" ||
    { echo "home sway test skipped (the stand-in game didn't build: $(head -n 3 "$RT/cc.log"))"; rm -rf "$RT"; exit 0; }

# the card: a two-screen game (its own run line) and a GBA game ROCKNIX's way (run-rocknix, RetroArch: the game panel)
mkdir -p "$RT/roms/both" "$RT/roms/gba" "$RT/data" "$RT/bin"
: > "$RT/roms/both/Two.nds"; : > "$RT/roms/gba/Game.gba"; echo "setup 2" > "$RT/data/setup-done"
# (the system's group for games, where this machine's cgroup v2 hierarchy can have one made)
MNT=$(awk '$3 == "cgroup2" { print $2 }' /proc/mounts | head -n 1)
SLICE=
if [ -n "$MNT" ] && mkdir -p "$MNT/home-sway-test.slice/game.scope" 2>/dev/null; then SLICE=$MNT/home-sway-test.slice; fi
if [ -n "$SLICE" ]; then
    cat > "$RT/bin/game-both" <<EOF
#!/bin/sh
echo \$\$ > "$SLICE/game.scope/cgroup.procs" || exit 1
exec setsid -w "$RT/sway-game" "$RT/count" both
EOF
else
    cat > "$RT/bin/game-both" <<EOF
#!/bin/sh
exec "$RT/sway-game" "$RT/count" both
EOF
fi
cat > "$RT/bin/runemu.sh" <<EOF
#!/bin/sh
exec "$RT/sway-game" "$RT/count"
EOF
chmod +x "$RT/bin/game-both" "$RT/bin/runemu.sh"
cat > "$RT/systems.conf" <<EOF
[both]
name = Two screens
folder = $RT/roms/both
ext = nds
run = $RT/bin/game-both %ROM%

[gba]
name = Game Boy Advance
folder = $RT/roms/gba
ext = gba
run = $L/run-rocknix gba %ROM% --core=mgba --emulator=retroarch --controllers=""
EOF
printf 'gba.emulator=retroarch\ngba.core=mgba\n' > "$RT/system.cfg"
mkfifo "$RT/keys"

printf 'output HEADLESS-1 mode 1920x1080 position 0 0\noutput HEADLESS-2 mode 1240x1080 position 340 1080\ndefault_border none\n' > "$RT/sway.cfg"
export XDG_RUNTIME_DIR=$RT WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=2 WLR_LIBINPUT_NO_DEVICES=1 WLR_RENDERER=pixman
sway -c "$RT/sway.cfg" > "$OUT/home-sway.log" 2>&1 &
SWAY=$!
i=0; while [ -z "$(ls "$RT" 2>/dev/null | grep '^wayland-[0-9]*$')" ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i + 1)); done
export WAYLAND_DISPLAY=$(ls "$RT" | grep '^wayland-[0-9]*$' | head -n 1)
export SWAYSOCK=$(ls "$RT"/sway-ipc.* 2>/dev/null | head -n 1)
[ -n "$WAYLAND_DISPLAY" ] || { echo "FAIL home sway test: sway didn't start (see $OUT/home-sway.log)"; kill $SWAY 2>/dev/null; rm -rf "$RT"; exit 1; }
export SDL_VIDEODRIVER=wayland SDL_AUDIODRIVER=dummy
env BANANA_HOME_KEY=1 BANANA_GAME_CGROUP="$SLICE" BANANA_INPUT_FIFO="$RT/keys" BANANA_ROCKNIX=1 BANANA_TOP=HEADLESS-1 BANANA_DISPLAY=sdl \
    BANANA_LIBEXEC="$L" BANANA_RUNEMU="$RT/bin/runemu.sh" BANANA_DATA="$RT/data" BANANA_RUN="$RT/run" \
    BANANA_ROCKNIX_OPTIONS="$L/rocknix-options" ROCKNIX_SETTINGS="$RT/system.cfg" ROCKNIX_LOCK="$RT/lock" \
    ROCKNIX_ES_SYSTEMS="$HERE/rocknix/tests/es_systems.cfg" ROCKNIX_ES_FEATURES="$HERE/rocknix/tests/es_features.cfg" \
    BANANA_HANDHELD=/bin/false \
    "$UI" --assets "$HERE/ui/assets/rnds" --splash "$HERE/ui/assets/splash" --banana "$HERE/ui/assets/banana" \
    --systems "$RT/systems.conf" --roms "$RT/roms" --data "$RT/data" > "$OUT/home-sway-ui.log" 2>&1 &
UIP=$!
# the Home button (the pad's BTN_MODE, 316: hw.conf's HW_HOME_KEY when it names none) down or up, as evdev sends it
key() { python3 -I -c '
import struct, sys, time
t = time.time(); s, u = int(t), int((t % 1) * 1e6)
with open(sys.argv[1], "wb") as f:
    f.write(struct.pack("llHHi", s, u, 1, 316, int(sys.argv[2])) + struct.pack("llHHi", s, u, 0, 0, 0))
' "$RT/keys" "$1"; }
tap() { key 1; sleep 0.15; key 0; }
count() { cat "$RT/count" 2>/dev/null || echo 0; }
shot() { grim -o HEADLESS-1 "$OUT/home-sway-$1-top.png" 2>/dev/null; grim -o HEADLESS-2 "$OUT/home-sway-$1-bottom.png" 2>/dev/null; }
# a screenshot's game frame: the stand-in's left half is (n & 255, n >> 8, 90) and its right half green (top) or blue
# (bottom); "-" when the game isn't what is shown there
frame() { convert "$1" -format '%[fx:int(255*p{100,500}.r+.5)] %[fx:int(255*p{100,500}.g+.5)] %[fx:int(255*p{100,500}.b+.5)] %[fx:int(255*p{w-100,500}.g+.5)] %[fx:int(255*p{w-100,500}.b+.5)]' info: 2>/dev/null |
    awk '{ if ($3 == 90 && ($4 == 200 || $5 == 200)) print $1 + 256 * $2; else print "-" }'; }
# the menu's windows as sway has them (name@output, sorted)
wins() { swaymsg -t get_tree 2>/dev/null | python3 -I -c '
import json, sys
out = []
def walk(n, o=None):
    o = n["name"] if n.get("type") == "output" else o
    if n.get("pid") and n.get("name") and n.get("visible"):
        out.append("%s@%s" % (n["name"], o))
    for k in n.get("nodes", []) + n.get("floating_nodes", []):
        walk(k, o)
try:
    walk(json.load(sys.stdin))
except Exception:
    pass
print(" ".join(sorted(out)))
'; }
sleep 5
[ "$(wins)" = "banana-ui bottom@HEADLESS-2 banana-ui top@HEADLESS-1" ] || bad "the menu's windows at the start: $(wins)"

# one game, its button pressed: $1 its name, $2 its ROM, $3 what the bottom output shows while it plays (game or menu)
play() {
    g=$1
    rm -f "$RT/count"
    echo "$2" > /run/banana/launch
    i=0; while [ "$(count)" -lt 30 ] && [ $i -lt 100 ]; do sleep 0.1; i=$((i + 1)); done
    sleep 0.5
    shot "$g-game"
    a=$(count); sleep 0.5; b=$(count)
    [ "$b" -gt "$a" ] || bad "$g: the game isn't running ($a, $b)"
    [ "$(frame "$OUT/home-sway-$g-game-top.png")" != - ] || bad "$g: the game isn't on the top output"
    if [ "$3" = game ]; then
        [ "$(frame "$OUT/home-sway-$g-game-bottom.png")" != - ] || bad "$g: the game isn't on the bottom output"
    else
        [ "$(frame "$OUT/home-sway-$g-game-bottom.png")" = - ] || bad "$g: the game is on the bottom output (the game panel's)"
    fi
    # a tap: the menu on both outputs, the game frozen
    tap; sleep 1.5
    shot "$g-menu"
    f1=$(count); sleep 1; f2=$(count)
    [ "$f1" = "$f2" ] || bad "$g: the game goes on under the menu ($f1, $f2)"
    w=$(wins)
    case "$w" in *"banana-ui bottom@HEADLESS-2 banana-ui top@HEADLESS-1"*) ;; *) bad "$g: the menu's windows over the game: $w";; esac
    [ "$(frame "$OUT/home-sway-$g-menu-top.png")" = - ] || bad "$g: the game still shows on the top output under the menu"
    [ "$(frame "$OUT/home-sway-$g-menu-bottom.png")" = - ] || bad "$g: the game still shows on the bottom output under the menu"
    grep -q 'home: the game stopped, the menu up' "$OUT/home-sway-ui.log" || bad "$g: no 'home: the game stopped' in the log"
    # a tap again: the game back, going on from the frame it was stopped at
    tap; sleep 1.2
    shot "$g-back"
    n=$(frame "$OUT/home-sway-$g-back-top.png")
    a=$(count); sleep 0.5; b=$(count)
    [ "$b" -gt "$a" ] || bad "$g: the game didn't go on after the menu ($a, $b)"
    [ "$n" != - ] && [ "$n" -gt "$f2" ] || bad "$g: the top output after the menu shows frame $n (frozen at $f2)"
    [ "$n" != - ] && [ "$n" -lt $((f2 + 150)) ] || bad "$g: the game didn't go on from where it was stopped (frame $n, frozen at $f2)"
    case "$(wins)" in *"banana-ui top"*) bad "$g: the menu's top window is still there: $(wins)";; esac
    # held for three seconds: the quick menu over the bottom output, the game going on on the top one
    key 1; sleep 3.4; key 0; sleep 0.8
    shot "$g-quick"
    t1=$(frame "$OUT/home-sway-$g-quick-top.png")
    a=$(count); sleep 0.6; b=$(count)
    grim -o HEADLESS-1 "$OUT/home-sway-$g-quick2-top.png" 2>/dev/null
    t2=$(frame "$OUT/home-sway-$g-quick2-top.png")
    [ "$b" -gt "$a" ] || bad "$g: the game stopped under the quick menu ($a, $b)"
    [ "$t1" != - ] && [ "$t2" != - ] && [ "$t2" -gt "$t1" ] || bad "$g: the top output under the quick menu: frames $t1, $t2 (the game not drawn, or not going on)"
    [ "$(frame "$OUT/home-sway-$g-quick-bottom.png")" = - ] || bad "$g: the quick menu isn't over the bottom output"
    grep -q 'home: the game goes on, the quick menu up' "$OUT/home-sway-ui.log" || bad "$g: no 'the game goes on, the quick menu up' in the log"
    # (over a game on both outputs, the sheet slides over a picture of the game's bottom window, not over black)
    [ "$3" = game ] && ! grep -q "home: the sheet over the game's bottom output, over its picture" "$OUT/home-sway-ui.log" && bad "$g: the sheet's window didn't slide over the game's picture"
    # the next press: the sheet down again, the game's bottom window (or the game panel) back
    tap; sleep 1.2
    shot "$g-after"
    if [ "$3" = game ]; then
        [ "$(frame "$OUT/home-sway-$g-after-bottom.png")" != - ] || bad "$g: the game's bottom window isn't back after the quick menu"
    fi
    [ "$(frame "$OUT/home-sway-$g-after-top.png")" != - ] || bad "$g: the game isn't on the top output after the quick menu"
    # the game ends: the menu's windows back
    pkill -f "$RT/sway-game" 2>/dev/null
    sleep 3
    [ "$(wins)" = "banana-ui bottom@HEADLESS-2 banana-ui top@HEADLESS-1" ] || bad "$g: the menu's windows after the game: $(wins)"
}
play both "$RT/roms/both/Two.nds" game
play panel "$RT/roms/gba/Game.gba" menu

kill $UIP 2>/dev/null; sleep 0.5; kill $SWAY 2>/dev/null; wait 2>/dev/null
grep -q 'home: no control group' "$OUT/home-sway-ui.log" && echo "home sway test: (no control group here: the game stopped by signals)"
[ -n "$SLICE" ] || echo "home sway test: (no cgroup v2 hierarchy to make a system's group in: the two-screen game in the menu's)"
[ -n "$SLICE" ] && rmdir "$SLICE/game.scope" "$SLICE" 2>/dev/null
rm -rf "$RT"
[ $fail = 0 ] && echo "home sway test: the menu over the frozen game and back, the quick menu over the bottom while the game plays ($OUT/home-sway-*.png)"
exit $fail

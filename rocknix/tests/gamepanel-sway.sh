#!/bin/sh
# The game panel under sway, as on ROCKNIX: banana-ui (SDL=1) as the frontend on two headless outputs the Thor's size,
# a RetroArch game started through run-rocknix with a stand-in runemu.sh. While it plays: the menu's top window gone
# (the game's output), its bottom one kept for the panel, the panel's command pipe handed down, a command written on
# it reaching RetroArch's network port (ra-netcmd). After: both windows back.
# Needs sway, swaymsg, python3 and a banana-ui built with SDL=1; root (the menu's /run/banana); skipped otherwise.
#   rocknix/tests/gamepanel-sway.sh
HERE=$(cd "$(dirname "$0")/../.." && pwd)
UI=${BANANA_UI:-$HERE/ui/banana-ui}
OUT=${OUT:-$HERE/ui/tests/out}
for t in sway swaymsg python3; do command -v $t >/dev/null || { echo "game panel sway test skipped (no $t)"; exit 0; }; done
ldd "$UI" 2>/dev/null | grep -q libSDL2 || { echo "game panel sway test skipped (banana-ui without SDL=1)"; exit 0; }
[ "$(id -u)" = 0 ] || { echo "game panel sway test skipped (not root: /run/banana)"; exit 0; }
mkdir -p "$OUT" /run/banana
L=$HERE/system/rootfs/usr/libexec/banana
RT=$(mktemp -d); chmod 700 "$RT"
fail=0
bad() { echo "FAIL game panel sway test: $*"; fail=1; }
# the card: one GBA game, ROCKNIX's way (its run line through run-rocknix, RetroArch by default)
mkdir -p "$RT/roms/gba" "$RT/data" "$RT/bin"
: > "$RT/roms/gba/Game.gba"; echo "setup 2" > "$RT/data/setup-done"
printf '[gba]\nname = Game Boy Advance\nfolder = %s\next = gba\nrun = %s/run-rocknix gba %%ROM%% --core=mgba --emulator=retroarch --controllers=""\n' \
    "$RT/roms/gba" "$L" > "$RT/systems.conf"
# the stand-in runemu.sh: RetroArch's network port (what reaches it, written down), what it was given, then a command
# on the panel's pipe as the panel would write it, and a few seconds of play
cat > "$RT/bin/runemu.sh" <<EOF
#!/bin/sh
env | grep -E '^BANANA_(RA_CMD|RA_OUT|CORE|TOP)=' > "$RT/env"
python3 -I -c '
import socket, sys, time
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.bind(("127.0.0.1", 55355)); s.settimeout(0.2)
end = time.time() + 5
with open(sys.argv[1], "w") as f:
    while time.time() < end:
        try:
            d, a = s.recvfrom(400); f.write(d.decode() + "\\n"); f.flush(); s.sendto(b"GET_STATUS PLAYING", a)
        except socket.timeout:
            pass
' "$RT/udp" &
sleep 1.5
swaymsg -t get_tree > "$RT/tree-game.json" 2>/dev/null
command -v grim >/dev/null && grim -o HEADLESS-2 "$OUT/gamepanel-bottom.png" 2>/dev/null
[ -n "\$BANANA_RA_CMD" ] && printf 'SAVE_STATE\n' > "\$BANANA_RA_CMD"
sleep 3
wait
EOF
chmod +x "$RT/bin/runemu.sh"
printf 'output HEADLESS-1 mode 1920x1080 position 0 0\noutput HEADLESS-2 mode 1240x1080 position 340 1080\ndefault_border none\n' > "$RT/sway.cfg"
export XDG_RUNTIME_DIR=$RT WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=2 WLR_LIBINPUT_NO_DEVICES=1 WLR_RENDERER=pixman
sway -c "$RT/sway.cfg" > "$OUT/gamepanel-sway.log" 2>&1 &
SWAY=$!
i=0; while [ -z "$(ls "$RT" 2>/dev/null | grep '^wayland-[0-9]*$')" ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i + 1)); done
export WAYLAND_DISPLAY=$(ls "$RT" | grep '^wayland-[0-9]*$' | head -n 1)
export SWAYSOCK=$(ls "$RT"/sway-ipc.* 2>/dev/null | head -n 1)
printf 'gba.emulator=retroarch\ngba.core=mgba\n' > "$RT/system.cfg"
env BANANA_ROCKNIX=1 BANANA_TOP=HEADLESS-1 BANANA_DISPLAY=sdl SDL_VIDEODRIVER=wayland SDL_AUDIODRIVER=dummy \
    BANANA_LIBEXEC="$L" BANANA_RUNEMU="$RT/bin/runemu.sh" BANANA_DATA="$RT/data" BANANA_RUN="$RT/run" \
    BANANA_ROCKNIX_OPTIONS="$L/rocknix-options" ROCKNIX_SETTINGS="$RT/system.cfg" ROCKNIX_LOCK="$RT/lock" \
    ROCKNIX_ES_SYSTEMS="$HERE/rocknix/tests/es_systems.cfg" ROCKNIX_ES_FEATURES="$HERE/rocknix/tests/es_features.cfg" \
    BANANA_HANDHELD=/bin/false \
    "$UI" --assets "$HERE/ui/assets/rnds" --splash "$HERE/ui/assets/splash" --banana "$HERE/ui/assets/banana" \
    --systems "$RT/systems.conf" --roms "$RT/roms" --data "$RT/data" > "$OUT/gamepanel-ui.log" 2>&1 &
UIP=$!
sleep 4
swaymsg -t get_tree > "$RT/tree-menu.json" 2>/dev/null
echo "$RT/roms/gba/Game.gba" > /run/banana/launch
sleep 8
swaymsg -t get_tree > "$RT/tree-after.json" 2>/dev/null
kill $UIP 2>/dev/null; sleep 0.5; kill $SWAY 2>/dev/null; wait 2>/dev/null
cp "$RT"/tree-*.json "$OUT/" 2>/dev/null
wins() { python3 -I - "$1" <<'PY'
import json, sys
try:
    t = json.load(open(sys.argv[1]))
except Exception:
    print('none'); sys.exit()
out = []
def walk(n, o=None):
    o = n['name'] if n.get('type') == 'output' else o
    if n.get('pid') and n.get('name'):
        out.append('%s@%s' % (n['name'], o))
    for k in n.get('nodes', []) + n.get('floating_nodes', []):
        walk(k, o)
walk(t)
print(' '.join(sorted(out)))
PY
}
[ "$(wins "$RT/tree-menu.json")" = "banana-ui bottom@HEADLESS-2 banana-ui top@HEADLESS-1" ] || bad "the menu's windows: $(wins "$RT/tree-menu.json")"
[ "$(wins "$RT/tree-game.json")" = "banana-ui bottom@HEADLESS-2" ] || bad "while the game plays: $(wins "$RT/tree-game.json")"
grep -q '^BANANA_RA_CMD=/run/banana/retroarch.cmd$' "$RT/env" 2>/dev/null || bad "no command pipe for the game ($(cat "$RT/env" 2>/dev/null))"
grep -qx 'SAVE_STATE' "$RT/udp" 2>/dev/null || bad "the panel's command didn't reach RetroArch's port ($(cat "$RT/udp" 2>/dev/null))"
[ "$(wins "$RT/tree-after.json")" = "banana-ui bottom@HEADLESS-2 banana-ui top@HEADLESS-1" ] || bad "after the game: $(wins "$RT/tree-after.json")"
rm -rf "$RT"
[ $fail = 0 ] && echo "game panel sway test: the game's screen given, the panel kept, its commands to RetroArch's port"
exit $fail

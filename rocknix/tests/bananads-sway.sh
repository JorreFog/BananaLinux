#!/bin/sh
# BananaDS's two windows under sway, as on ROCKNIX (rocknix/packages/emulators/standalone/bananads-sa: its SDL frontend
# with patches/0001): sway headless with two outputs the AYN Thor's screens' size and ROCKNIX's own rule for a second
# window (111-sway-init: a title with "Bottom" moves to the second output), BananaDS started on the test ROM, then
# each output's screenshot ($OUT/bananads-top.png, -bottom.png): each window full screen on its own output, drawn.
# Needs sway, grim and a BananaDS built with SDL and the patch (BANANADS=<binary>); skipped otherwise.
#   BANANADS=.../bananads rocknix/tests/bananads-sway.sh
HERE=$(cd "$(dirname "$0")/../.." && pwd)
OUT=${OUT:-$HERE/ui/tests/out}
[ -x "$BANANADS" ] || { echo "bananads sway test skipped (no BANANADS binary)"; exit 0; }
for t in sway swaymsg grim convert; do command -v $t >/dev/null || { echo "bananads sway test skipped (no $t)"; exit 0; }; done
mkdir -p "$OUT"
RT=$(mktemp -d); chmod 700 "$RT"
cat > "$RT/sway.cfg" <<CFG
output HEADLESS-1 mode 1920x1080 position 0 0
output HEADLESS-2 mode 1240x1080 position 340 1080
default_border none
for_window [title=".*(Secondary|\[w2\]|Sub|Bottom|Screen 2|GamePad).*"] move window to output HEADLESS-2
CFG
export XDG_RUNTIME_DIR=$RT WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=2 WLR_LIBINPUT_NO_DEVICES=1 WLR_RENDERER=pixman
sway -c "$RT/sway.cfg" > "$OUT/bananads-sway.log" 2>&1 &
SWAY=$!
i=0; while [ -z "$(ls "$RT" 2>/dev/null | grep '^wayland-[0-9]*$')" ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i + 1)); done
export WAYLAND_DISPLAY=$(ls "$RT" | grep '^wayland-[0-9]*$' | head -n 1)
export SWAYSOCK=$(ls "$RT"/sway-ipc.* 2>/dev/null | head -n 1)
fail=0
mkdir -p "$RT/data"
SDL_VIDEODRIVER=wayland SDL_AUDIODRIVER=dummy BANANADS_DATA="$RT/data" "$BANANADS" --set frontend=sdl --set sdl.windows=2 \
    --set mic.source=none --set resume.on_quit=0 --set resume.on_start=0 "$HERE/tools/test-roms/ds2dtest.nds" > "$OUT/bananads-run.log" 2>&1 &
EMU=$!
sleep 6
grim -o HEADLESS-1 "$OUT/bananads-top.png" 2>/dev/null
grim -o HEADLESS-2 "$OUT/bananads-bottom.png" 2>/dev/null
swaymsg -t get_tree > "$OUT/bananads-tree.json" 2>/dev/null
kill $EMU 2>/dev/null; sleep 1; kill -9 $EMU 2>/dev/null; kill $SWAY 2>/dev/null; wait 2>/dev/null
rm -rf "$RT"
python3 -I - "$OUT/bananads-tree.json" <<'PY' || fail=1
import json, sys
t = json.load(open(sys.argv[1]))
where = {}
def walk(n, out=None):
    if n.get('type') == 'output':
        out = n['name']
    if n.get('name') in ('BananaDS', 'BananaDS Bottom') and n.get('pid'):
        where[n['name']] = (out, n.get('fullscreen_mode'), n['rect']['width'], n['rect']['height'])
    for k in n.get('nodes', []) + n.get('floating_nodes', []):
        walk(k, out)
walk(t)
ok = where.get('BananaDS', ('',))[0] == 'HEADLESS-1' and where.get('BananaDS Bottom', ('',))[0] == 'HEADLESS-2'
ok = ok and all(v[1] == 1 for v in where.values())
if not ok:
    print('FAIL bananads sway test: the windows: %s' % where)
sys.exit(0 if ok else 1)
PY
for f in bananads-top bananads-bottom; do
    [ "$(convert "$OUT/$f.png" -format '%[fx:mean]' info: 2>/dev/null)" != 0 ] || { echo "FAIL bananads sway test: $f is black"; fail=1; }
done
[ $fail = 0 ] && echo "bananads sway test: a window on each output, full screen, drawn ($OUT/bananads-top.png, bananads-bottom.png)"
exit $fail

#!/bin/sh
# banana-ui under a Wayland compositor, as on ROCKNIX (the SDL display, src/kms_sdl.cpp): sway headless with two
# outputs the AYN Thor's screens' size (HEADLESS-1 1920x1080 as the top, HEADLESS-2 1240x1080 as the bottom), the menu
# started on them, then a screenshot of each output ($OUT/sway-top.png, sway-bottom.png) and their checks: each
# window fullscreen on its own output, the picture fitted (the bars black, the middle drawn). Needs sway, grim and a
# banana-ui built with SDL=1 (make -C ui host SDL=1); skipped otherwise.
#   ui/tests/sway-test.sh
HERE=$(cd "$(dirname "$0")/.." && pwd)
OUT=${OUT:-$HERE/tests/out}
for t in sway swaymsg grim convert; do command -v $t >/dev/null || { echo "sway test skipped (no $t)"; exit 0; }; done
ldd "$HERE/banana-ui" 2>/dev/null | grep -q libSDL2 || { echo "sway test skipped (banana-ui without SDL=1)"; exit 0; }
mkdir -p "$OUT"
FX=$OUT/fixture
[ -d "$FX/roms" ] || "$HERE/tests/fixture.sh" "$FX" >/dev/null
RT=$(mktemp -d); chmod 700 "$RT"
# a card that has been set up (no first-boot arrival or setup: the menu itself on both screens)
D=$RT/data; cp -r "$FX/data" "$D"; echo "setup 2" > "$D/setup-done"
CFG=$RT/sway.cfg
cat > "$CFG" <<EOF
output HEADLESS-1 mode 1920x1080 position 0 0
output HEADLESS-2 mode 1240x1080 position 340 1080
default_border none
EOF
export XDG_RUNTIME_DIR=$RT WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=2 WLR_LIBINPUT_NO_DEVICES=1 WLR_RENDERER=pixman
sway -c "$CFG" > "$OUT/sway.log" 2>&1 &
SWAY=$!
i=0; while [ -z "$(ls "$RT" 2>/dev/null | grep '^wayland-[0-9]*$')" ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i + 1)); done
export WAYLAND_DISPLAY=$(ls "$RT" | grep '^wayland-[0-9]*$' | head -n 1)
export SWAYSOCK=$(ls "$RT"/sway-ipc.* 2>/dev/null | head -n 1)
fail=0
if [ -z "$WAYLAND_DISPLAY" ]; then echo "FAIL sway test: sway didn't start (see $OUT/sway.log)"; kill $SWAY 2>/dev/null; rm -rf "$RT"; exit 1; fi
BANANA_TOP=HEADLESS-1 SDL_VIDEODRIVER=wayland "$HERE/banana-ui" --assets "$HERE/assets/rnds" --splash "$HERE/assets/splash" \
    --banana "$HERE/assets/banana" --systems "$HERE/../system/rootfs/etc/banana/systems.conf" --roms "$FX/roms" \
    --data "$D" > "$OUT/sway-banana-ui.log" 2>&1 &
UI=$!
sleep 8
grim -o HEADLESS-1 "$OUT/sway-top.png" 2>/dev/null
grim -o HEADLESS-2 "$OUT/sway-bottom.png" 2>/dev/null
swaymsg -t get_tree > "$OUT/sway-tree.json" 2>/dev/null
kill $UI 2>/dev/null; sleep 0.5; kill $SWAY 2>/dev/null; wait 2>/dev/null
rm -rf "$RT"

grep -q 'display (SDL wayland): screen 0 on display .*1920x1080' "$OUT/sway-banana-ui.log" ||
    { echo "FAIL sway test: the top screen isn't the 1920x1080 output ($(grep -m2 'display' "$OUT/sway-banana-ui.log"))"; fail=1; }
grep -q 'display (SDL wayland): screen 1 on display .*1240x1080' "$OUT/sway-banana-ui.log" ||
    { echo "FAIL sway test: the bottom screen isn't the 1240x1080 output"; fail=1; }
# a pixel's colour: black in the bars, not black in the picture
px() { convert "$1" -format "%[pixel:p{$2,$3}]" info: 2>/dev/null; }
for f in sway-top sway-bottom; do [ -s "$OUT/$f.png" ] || { echo "FAIL sway test: no screenshot $f"; fail=1; }; done
if [ $fail = 0 ]; then
    # the top: 1440x1080 in the middle of 1920, bars of 240 each side
    [ "$(px "$OUT/sway-top.png" 100 540)" = "srgb(0,0,0)" ] || { echo "FAIL sway test: the top's left bar isn't black"; fail=1; }
    [ "$(convert "$OUT/sway-top.png" -crop 1440x1080+240+0 -format '%[fx:mean]' info:)" != 0 ] || { echo "FAIL sway test: the top's picture is black"; fail=1; }
    # the bottom: 1240x930 in the middle of 1240x1080, bars of 75 above and below
    [ "$(px "$OUT/sway-bottom.png" 620 30)" = "srgb(0,0,0)" ] || { echo "FAIL sway test: the bottom's top bar isn't black"; fail=1; }
    [ "$(convert "$OUT/sway-bottom.png" -crop 1240x930+0+75 -format '%[fx:mean]' info:)" != 0 ] || { echo "FAIL sway test: the bottom's picture is black"; fail=1; }
fi
[ $fail = 0 ] && echo "sway test: both screens on their outputs, fitted ($OUT/sway-top.png, $OUT/sway-bottom.png)"
exit $fail

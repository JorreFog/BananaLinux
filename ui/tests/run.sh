#!/bin/sh
# banana-ui's headless test: a test library, then the menu's three views at both panel sizes, checked for what
# must hold (the right view after each button, a game launched from the ready screen, frames rendered) and
# timed. The PNGs are left in $OUT for a look.
set -e
HERE=$(cd "$(dirname "$0")/.." && pwd)
OUT=${OUT:-$HERE/tests/out}
FX=$OUT/fixture
rm -rf "$OUT"; mkdir -p "$OUT"
"$HERE/tests/fixture.sh" "$FX" >/dev/null
fail=0
for size in 640x480 1024x768; do
    log=$OUT/$size.log
    "$HERE/banana-ui" --headless "$OUT/$size-" --size $size --assets "$HERE/assets/rnds" \
        --splash "$HERE/assets/splash" --banana "$HERE/assets/banana" \
        --systems "$HERE/../system/rootfs/etc/banana/systems.conf" --roms "$FX/roms" --data "$FX/data" \
        --script "splash boot-drop 200; splash boot-word 700; splash boot-click 1100 1; home 0; wait 2400; shot home; bench 120; press a; wait 2400; shot lib; press right; wait 2400; shot lib2; bench 120; press a; wait 3000; shot ready; press b; press b; press start; press down; shot settings; press r; press r; press r; press r; press r; shot settings-system; press right; shot settings-tz; press b; battery 50; battery 19; shot battery-low; press left; battery 18; battery 4; wait 2400; shot battery-empty; battery 4 charging; battery 19" \
        2> "$log" || { echo "FAIL $size: banana-ui exited $?"; fail=1; continue; }
    check() { grep -q "$1" "$log" || { echo "FAIL $size: no '$1'"; fail=1; }; }
    check "press a: used, view 1"          # home -> the DS library
    check "press right: used, view 1 sys 0 game 1"
    check "press a: used, view 2"          # library -> ready screen
    check "launch 1: .*Monkey Puzzle League.nds"
    check "splash boot-click at 1100 ms"
    check "press start: settings open"
    check "press b: settings closed"
    grep -q '^timezone = [^U]' "$FX/data/settings.conf" || { echo "FAIL $size: the time zone wasn't saved"; fail=1; }
    check "battery 19%: notice Battery low: 19%"
    check "press left: notice hidden"
    check "battery 4%: notice Battery nearly empty: 4%"
    check "battery 4%: notice hidden"      # the charger on
    [ "$(grep -c 'notice Battery low: 19%' "$log")" = 2 ] || { echo "FAIL $size: the notice didn't come back after charging"; fail=1; }
    grep -q "battery 18%" "$log" && { echo "FAIL $size: a second notice in the same step"; fail=1; }
    for s in boot-drop boot-word boot-click home lib lib2 ready settings settings-system settings-tz battery-low battery-empty; do
        for p in top bot; do
            f=$OUT/$size-$s-$p.png
            [ -s "$f" ] || { echo "FAIL $size: no $f"; fail=1; continue; }
            # a frame that is one flat colour means nothing was drawn
            [ "$(convert "$f" -format '%[fx:standard_deviation>0.01]' info:)" = 1 ] || { echo "FAIL $size: $f is blank"; fail=1; }
        done
    done
    grep 'bench:' "$log" | sed "s/^banana-ui: /$size /"
done
[ $fail = 0 ] && echo "banana-ui: all checks passed ($OUT)"
exit $fail

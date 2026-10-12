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
        --script "splash boot-drop 200; splash boot-word 700; splash boot-click 1100 1; home 0; wait 2400; shot home; bench 120; press a; wait 2400; shot lib; press right; wait 2400; shot lib2; bench 120; press a; wait 3000; shot ready; press b; press b; press start; shot settings; press a; press down; shot settings-list; press r; press r; press r; press r; press r; shot settings-system; press right; shot settings-tz; press b; press b; romicon $FX/roms/nds/Yellow Planet [!].zip; romicon $FX/roms/nds/Peel Quest.nds; battery 50; battery 19; shot battery-low; press left; battery 18; battery 4; wait 2400; shot battery-empty; battery 4 charging; battery 19" \
        2> "$log" || { echo "FAIL $size: banana-ui exited $?"; fail=1; continue; }
    check() { grep -q "$1" "$log" || { echo "FAIL $size: no '$1'"; fail=1; }; }
    check "press a: used, view 1"          # home -> the DS library
    check "press right: used, view 1 sys 0 game 1"
    check "press a: used, view 2"          # library -> ready screen
    check "launch 1: .*Monkey Puzzle League.nds"
    check "splash boot-click at 1100 ms"
    check "press start: settings open"
    check "press b: settings closed"
    check "romicon .*Yellow Planet \[!\].zip: 32x32, middle #ffe721"     # the icon from inside the zip
    check "romicon .*Peel Quest.nds: none"                             # no banner
    grep -q '^timezone = [^U]' "$FX/data/settings.conf" || { echo "FAIL $size: the time zone wasn't saved"; fail=1; }
    check "battery 19%: notice Battery low: 19%"
    check "press left: notice hidden"
    check "battery 4%: notice Battery nearly empty: 4%"
    check "battery 4%: notice hidden"      # the charger on
    [ "$(grep -c 'notice Battery low: 19%' "$log")" = 2 ] || { echo "FAIL $size: the notice didn't come back after charging"; fail=1; }
    grep -q "battery 18%" "$log" && { echo "FAIL $size: a second notice in the same step"; fail=1; }
    for s in boot-drop boot-word boot-click home lib lib2 ready settings settings-list settings-system settings-tz battery-low battery-empty; do
        for p in top bot; do
            f=$OUT/$size-$s-$p.png
            [ -s "$f" ] || { echo "FAIL $size: no $f"; fail=1; continue; }
            # a frame that is one flat colour means nothing was drawn
            [ "$(convert "$f" -format '%[fx:standard_deviation>0.01]' info:)" = 1 ] || { echo "FAIL $size: $f is blank"; fail=1; }
        done
    done
    # Wi-Fi: Settings > Network, the networks from a stand-in wifi script, a password typed by touch, joining,
    # forgetting, an open network and a WPA3-only one (Settings opens on its groups: R four times is Network)
    wlog=$OUT/$size-wifi.log
    rm -f "$FX/data/wifi.conf"
    key() { for rc in "$@"; do awk -v rc=$rc 'BEGIN { split(rc, a, ","); printf "touch 1 %.0f %.0f; ", 18 + a[2] * 60.4 + 27, 80 + a[1] * 66 + 29 }'; done; }
    BANANA_WIFI="$HERE/tests/fake-wifi.sh" FAKE_WIFI_CONF="$FX/data/wifi.conf" "$HERE/banana-ui" --headless "$OUT/$size-" --size $size \
        --assets "$HERE/assets/rnds" --systems "$HERE/../system/rootfs/etc/banana/systems.conf" --roms "$FX/roms" --data "$FX/data" \
        --script "home 0; press start; press r; press r; press r; press r; press a; press down; shot wifi-row; press down; shot share-row; press up; press a; shot wifi-list; press a; \
            $(key 3,4 2,0 3,5) press start; shot wifi-short; $(key 2,0 3,5 2,0 0,0 0,1) shot wifi-keys; press start; wait 2500; shot wifi-joined; \
            press y; press down; press down; press a; wait 2500; press up; press a; press x; $(key 1,0 1,1 1,2 1,3 1,4 1,5 4,0 1,6 1,7) press start; \
            wait 2500; shot wifi-end" 2> "$wlog" || { echo "FAIL $size: banana-ui (Wi-Fi) exited $?"; fail=1; }
    conf=$FX/data/wifi.conf
    [ "$(sed -n 's/^[[:space:]]*ssid=//p' "$conf" | tr '\n' '|')" = '"Neighbour 5G"|"Cafe Free"|' ] || { echo "FAIL $size: wifi.conf networks: $(grep ssid= "$conf" | tr -d '\t' | tr '\n' ' ')"; fail=1; }
    grep -q 'psk="QWERTYui"' "$conf" && grep -q 'key_mgmt=SAE' "$conf" && grep -q 'ieee80211w=2' "$conf" || { echo "FAIL $size: the WPA3 network's block"; fail=1; }
    grep -q 'key_mgmt=NONE' "$conf" || { echo "FAIL $size: the open network's block"; fail=1; }
    grep -q 'priority=2' "$conf" || { echo "FAIL $size: the last network joined isn't first"; fail=1; }
    for s in wifi-row wifi-list wifi-short wifi-keys wifi-joined wifi-end; do
        [ -s "$OUT/$size-$s-top.png" ] && [ -s "$OUT/$size-$s-bot.png" ] || { echo "FAIL $size: no $s shot"; fail=1; }
    done
    # The first-time setup: opened as on a first boot, a RetroAchievements account typed by touch (name, then the
    # password), Finish writes setup-done; the accounts file is what the launchers read (no login with the server
# here: BANANA_RA_CHECK=0)
    slog=$OUT/$size-setup.log
    rm -f "$FX/data/setup-done" "$FX/data/accounts.conf"
    BANANA_RA_CHECK=0 BANANA_WIFI="$HERE/tests/fake-wifi.sh" FAKE_WIFI_CONF="$FX/data/wifi.conf" "$HERE/banana-ui" --headless "$OUT/$size-" --size $size \
        --assets "$HERE/assets/rnds" --systems "$HERE/../system/rootfs/etc/banana/systems.conf" --roms "$FX/roms" --data "$FX/data" \
        --script "home 0; setup; shot setup; press right; shot setup-tz; press down; press down; shot setup-ra; press a; shot setup-name; \
            $(key 1,0 1,1 1,2) press start; shot setup-pass; $(key 2,0 2,1) press start; shot setup-signed; press down; press a; shot setup-done" 2> "$slog" \
        || { echo "FAIL $size: banana-ui (setup) exited $?"; fail=1; }
    grep -q "setup open" "$slog" || { echo "FAIL $size: the setup didn't open"; fail=1; }
    [ -f "$FX/data/setup-done" ] || { echo "FAIL $size: Finish didn't write setup-done"; fail=1; }
    grep -q '^ra_user=qwe$' "$FX/data/accounts.conf" && grep -q '^ra_password=as$' "$FX/data/accounts.conf" || { echo "FAIL $size: accounts.conf: $(cat "$FX/data/accounts.conf" 2>/dev/null | tr '\n' ' ')"; fail=1; }
    for s in setup setup-tz setup-ra setup-name setup-pass setup-signed; do
        [ -s "$OUT/$size-$s-top.png" ] && [ -s "$OUT/$size-$s-bot.png" ] || { echo "FAIL $size: no $s shot"; fail=1; }
    done
    rm -f "$FX/data/setup-done" "$FX/data/accounts.conf"
    grep 'bench:' "$log" | sed "s/^banana-ui: /$size /"
done
[ $fail = 0 ] && echo "banana-ui: all checks passed ($OUT)"
exit $fail

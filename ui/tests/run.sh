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
# the panel rotation's transpose (src/turn.h): the tiled path must match the scalar loop for any rectangle. QEMU's
# panel reports no orientation, so this is what covers the handheld's 90-degree panel.
${CXX:-c++} -O2 -std=c++17 -Wno-unused-function -o "$OUT/turn-test" "$HERE/tests/turn-test.cpp" 2> "$OUT/turn-build.log" ||
    { echo "FAIL: can't build turn-test (see $OUT/turn-build.log)"; fail=1; }
[ "$fail" = 1 ] || "$OUT/turn-test" > "$OUT/turn.log" 2>&1 || { echo "FAIL: turn-test"; cat "$OUT/turn.log"; fail=1; }
for size in 640x480 1024x768; do
    log=$OUT/$size.log
    "$HERE/banana-ui" --headless "$OUT/$size-" --size $size --assets "$HERE/assets/rnds" \
        --splash "$HERE/assets/splash" --banana "$HERE/assets/banana" \
        --systems "$HERE/tests/systems.conf" --roms "$FX/roms" --data "$FX/data" \
        --script "boot; splash boot-drop 200; splash boot-word 700; splash boot-click 1100 1; home 0; wait 2400; shot home; bench 120; press a; wait 2400; shot lib; press right; wait 2400; shot lib2; bench 120; press a; wait 3000; shot ready; press b; press b; press start; shot settings; press a; press down; shot settings-list; press r; press r; press r; press r; shot settings-system; press right; shot settings-tz; press b; press b; romicon $FX/roms/nds/Yellow Planet [!].zip; romicon $FX/roms/nds/Peel Quest.nds; battery 50; battery 19; shot battery-low; press left; battery 18; battery 4; wait 2400; shot battery-empty; battery 4 charging; battery 19" \
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
    # the splash is one screen now (no second panel, no console/cartridge): only -top is written for it
    for s in boot-drop boot-word boot-click; do
        f=$OUT/$size-$s-top.png
        [ -s "$f" ] || { echo "FAIL $size: no $f"; fail=1; continue; }
        [ "$(convert "$f" -format '%[fx:standard_deviation>0.01]' info:)" = 1 ] || { echo "FAIL $size: $f is blank"; fail=1; }
    done
    for s in home lib lib2 ready settings settings-list settings-system settings-tz battery-low battery-empty; do
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
        --assets "$HERE/assets/rnds" --systems "$HERE/tests/systems.conf" --roms "$FX/roms" --data "$FX/data" \
        --script "home 0; press start; press r; press r; press r; press a; press down; shot wifi-row; press down; shot share-row; press up; press a; shot wifi-list; press a; \
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
    # password), past the Box art step (not started: no network here), Finish writes setup-done; the accounts file is what the launchers read (no login with the server
# here: BANANA_RA_CHECK=0)
    slog=$OUT/$size-setup.log
    rm -f "$FX/data/setup-done" "$FX/data/accounts.conf"
    BANANA_RA_CHECK=0 BANANA_WIFI="$HERE/tests/fake-wifi.sh" FAKE_WIFI_CONF="$FX/data/wifi.conf" "$HERE/banana-ui" --headless "$OUT/$size-" --size $size \
        --assets "$HERE/assets/rnds" --systems "$HERE/tests/systems.conf" --roms "$FX/roms" --data "$FX/data" \
        --script "home 0; setup; shot setup; press right; shot setup-tz; press down; press down; shot setup-ra; press a; shot setup-name; \
            $(key 1,0 1,1 1,2) press start; shot setup-pass; $(key 2,0 2,1) press start; shot setup-signed; press down; shot setup-boxart; press down; press a; shot setup-done" 2> "$slog" \
        || { echo "FAIL $size: banana-ui (setup) exited $?"; fail=1; }
    grep -q "setup open" "$slog" || { echo "FAIL $size: the setup didn't open"; fail=1; }
    [ -f "$FX/data/setup-done" ] || { echo "FAIL $size: Finish didn't write setup-done"; fail=1; }
    grep -q '^ra_user=qwe$' "$FX/data/accounts.conf" && grep -q '^ra_password=as$' "$FX/data/accounts.conf" || { echo "FAIL $size: accounts.conf: $(cat "$FX/data/accounts.conf" 2>/dev/null | tr '\n' ' ')"; fail=1; }
    for s in setup setup-tz setup-ra setup-name setup-pass setup-signed; do
        [ -s "$OUT/$size-$s-top.png" ] && [ -s "$OUT/$size-$s-bot.png" ] || { echo "FAIL $size: no $s shot"; fail=1; }
    done
    rm -f "$FX/data/setup-done" "$FX/data/accounts.conf"
    # Favorites starred itself (first on the shelf, ahead of every system): a GBA favourite launches with GBA's
    # emulator, not the shelf's one along (its systems' places moved when Favorites went in before them)
    flog=$OUT/$size-favorites.log
    cp "$FX/data/favorites.txt" "$OUT/favorites.txt.keep"
    echo "gba/Orchard Tactics.gba" > "$FX/data/favorites.txt"; echo favorites > "$FX/data/favorite-systems.txt"
    "$HERE/banana-ui" --headless "$OUT/$size-" --size $size --assets "$HERE/assets/rnds" \
        --systems "$HERE/tests/systems.conf" --roms "$FX/roms" --data "$FX/data" \
        --script "lib 0 0; press a; wait 3000; wait 1" 2> "$flog" || { echo "FAIL $size: banana-ui (favorites) exited $?"; fail=1; }
    grep -q "launch 0: .*Orchard Tactics.gba (system gba)" "$flog" || { echo "FAIL $size: the favourite: $(grep launch "$flog")"; fail=1; }
    mv "$OUT/favorites.txt.keep" "$FX/data/favorites.txt"; rm -f "$FX/data/favorite-systems.txt"
    # X on a game in the library: no per-game page any more (it opened Settings' DS emulator/shader page)
    xlog=$OUT/$size-xgame.log
    "$HERE/banana-ui" --headless "$OUT/$size-" --size $size --assets "$HERE/assets/rnds" \
        --systems "$HERE/tests/systems.conf" --roms "$FX/roms" --data "$FX/data" \
        --script "lib 0 0; press x; wait 300; shot xgame; wait 1" 2> "$xlog" ||
        { echo "FAIL $size: banana-ui (x on a game) exited $?"; fail=1; }
    grep -q "settings open" "$xlog" && { echo "FAIL $size: X opened a settings page"; fail=1; }
    [ -s "$OUT/$size-xgame-top.png" ] && [ -s "$OUT/$size-xgame-bot.png" ] || { echo "FAIL $size: no xgame shot"; fail=1; }
    # Background Music: the Display & sound row (the seventh) toggles settings.conf's bgm, off then back on
    grep -q '^bgm = ' "$FX/data/settings.conf" && sed -i 's/^bgm = .*/bgm = on/' "$FX/data/settings.conf" || printf 'bgm = on\n' >> "$FX/data/settings.conf"
    blog=$OUT/$size-bgm.log
    run_bgm() { "$HERE/banana-ui" --headless "$OUT/$size-" --size $size --assets "$HERE/assets/rnds" \
        --systems "$HERE/tests/systems.conf" --roms "$FX/roms" --data "$FX/data" --script "$1" 2>> "$blog" ||
        { echo "FAIL $size: banana-ui (bgm) exited $?"; fail=1; }; }
    run_bgm "home 0; press start; press a; press down; press down; press down; press down; press down; press down; press right; shot bgm-off"
    grep -q '^bgm = off' "$FX/data/settings.conf" || { echo "FAIL $size: Background Music Off didn't save: $(grep '^bgm' "$FX/data/settings.conf")"; fail=1; }
    run_bgm "home 0; press start; press a; press down; press down; press down; press down; press down; press down; press right; shot bgm-on"
    grep -q '^bgm = on' "$FX/data/settings.conf" || { echo "FAIL $size: Background Music On didn't save: $(grep '^bgm' "$FX/data/settings.conf")"; fail=1; }
    for s in bgm-off bgm-on; do [ -s "$OUT/$size-$s-top.png" ] || { echo "FAIL $size: no $s shot"; fail=1; }; done
    # the AMOLED burn-in guard: 90 s idle dims the panel to 5%, 180 s blanks it, and the first press restores it and
    # is swallowed (it must not act on the menu)
    glog=$OUT/$size-guard.log
    printf '200\n' > "$FX/bl"; printf '255\n' > "$FX/blmax"
    "$HERE/banana-ui" --headless "$OUT/$size-" --size $size --assets "$HERE/assets/rnds" \
        --systems "$HERE/tests/systems.conf" --roms "$FX/roms" --data "$FX/data" \
        --script "home 0; guard $FX/bl $FX/blmax; idle 90000; wait 1; shot guard-dim; idle 90000; wait 1; shot guard-blank; press a; wait 1; shot guard-wake" 2> "$glog" ||
        { echo "FAIL $size: banana-ui (guard) exited $?"; fail=1; }
    grep -q "guard: idle +90000 ms -> stage 1, level 12" "$glog" || { echo "FAIL $size: the guard didn't dim to 5%: $(grep 'guard: idle' "$glog" | tr '\n' '|')"; fail=1; }
    grep -q "guard: idle +90000 ms -> stage 2, level 0" "$glog" || { echo "FAIL $size: the guard didn't blank the panel"; fail=1; }
    grep -q "press a: swallowed by the burn-in guard" "$glog" || { echo "FAIL $size: waking the panel wasn't swallowed"; fail=1; }
    [ "$(cat "$FX/bl")" = 200 ] || { echo "FAIL $size: the guard didn't restore the brightness: $(cat "$FX/bl")"; fail=1; }
    for s in guard-dim guard-blank guard-wake; do [ -s "$OUT/$size-$s-top.png" ] || { echo "FAIL $size: no $s shot"; fail=1; }; done
    # Settings > System's two resets: the system one puts the menu's own keys back (theme, sounds, ...) and leaves
    # the emulator's (core.gba, ra_shader) alone; then the emulator one puts those back and leaves the menu's alone
    rlog=$OUT/$size-reset.log
    for kv in "theme = light" "sounds = off" "core.gba = mgba" "ra_shader = crt"; do
        k=${kv%% *}
        grep -q "^$k = " "$FX/data/settings.conf" && sed -i "s/^$k = .*/$kv/" "$FX/data/settings.conf" || printf '%s\n' "$kv" >> "$FX/data/settings.conf"
    done
    # groups: Display & sound, Sleep & power, Nintendo DS, Other systems, Network, System (five R's); System's rows
    # from Time zone: down seven to Reset system settings, A twice
    run_reset() { "$HERE/banana-ui" --headless "$OUT/$size-" --size $size --assets "$HERE/assets/rnds" \
        --systems "$HERE/tests/systems.conf" --roms "$FX/roms" --data "$FX/data" --script "$1" 2>> "$rlog" ||
        { echo "FAIL $size: banana-ui (reset) exited $?"; fail=1; }; }
    run_reset "home 0; press start; press r; press r; press r; press r; press a; press down; press down; press down; press down; press down; press down; press down; press a; press a; wait 300; shot reset-system"
    grep -q '^theme = dark' "$FX/data/settings.conf" || { echo "FAIL $size: system reset didn't restore theme: $(grep '^theme' "$FX/data/settings.conf")"; fail=1; }
    grep -q '^sounds = on' "$FX/data/settings.conf" || { echo "FAIL $size: system reset didn't restore sounds"; fail=1; }
    grep -q '^core.gba = mgba' "$FX/data/settings.conf" || { echo "FAIL $size: system reset wiped an emulator core: $(grep '^core.gba' "$FX/data/settings.conf")"; fail=1; }
    grep -q '^ra_shader = crt' "$FX/data/settings.conf" || { echo "FAIL $size: system reset wiped ra_shader"; fail=1; }
    # down eight from Time zone to Reset emulator settings, A twice (theme and sounds are already back)
    run_reset "home 0; press start; press r; press r; press r; press r; press a; press down; press down; press down; press down; press down; press down; press down; press down; press a; press a; wait 300; shot reset-emulator"
    grep -q '^core.gba = gpsp' "$FX/data/settings.conf" || { echo "FAIL $size: emulator reset didn't restore core.gba: $(grep '^core.gba' "$FX/data/settings.conf")"; fail=1; }
    grep -q '^ra_shader = off' "$FX/data/settings.conf" || { echo "FAIL $size: emulator reset didn't restore ra_shader"; fail=1; }
    grep -q '^theme = dark' "$FX/data/settings.conf" || { echo "FAIL $size: emulator reset wiped a system key"; fail=1; }
    for s in reset-system reset-emulator; do
        [ -s "$OUT/$size-$s-top.png" ] && [ -s "$OUT/$size-$s-bot.png" ] || { echo "FAIL $size: no $s shot"; fail=1; }
    done
    # a layout theme (the image's pocket.json, the handheld's own): every screen drawn from its JSON, home ->
    # library -> a launch, and a tap on a system. It is the single-panel layout, so it runs at its own 640x557.
    # The menu reads the theme from the card's settings.conf (--theme is the real launcher's), so point it at the
    # file for this run and put it back (the settings cases above check for dark).
    llog=$OUT/$size-layout.log
    sed -i "s|^ *theme *=.*|theme = file:$HERE/../system/rootfs/usr/share/banana/themes/pocket.json|" "$FX/data/settings.conf"
    "$HERE/banana-ui" --headless "$OUT/$size-layout-" --size 640x557 --single --assets "$HERE/assets/rnds" --banana "$HERE/assets/banana" \
        --systems "$HERE/tests/systems.conf" --roms "$FX/roms" --data "$FX/data" \
        --script "home 0; wait 1000; shot home; press right; wait 1000; shot home2; touch 0 180 250; wait 1000; press a; wait 1000; shot lib; \
            press down; wait 600; shot lib2; press a; wait 350; shot launch; wait 1000; wait 1" 2> "$llog" || { echo "FAIL $size: banana-ui (layout) exited $?"; fail=1; }
    sed -i "s|^ *theme *=.*|theme = dark|" "$FX/data/settings.conf"
    grep -q "theme:" "$llog" && { echo "FAIL $size: the layout theme: $(grep 'theme:' "$llog" | head -3)"; fail=1; }
    grep -q "touch 180 250: used" "$llog" || { echo "FAIL $size: the layout theme's tap on a system"; fail=1; }
    grep -q "press down: used, view 1 sys 0 game 1" "$llog" || { echo "FAIL $size: the layout theme's own buttons (down: the next game)"; fail=1; }
    grep -q "launch 1: .*Monkey Puzzle League.nds" "$llog" || { echo "FAIL $size: the layout theme's launch: $(grep launch "$llog")"; fail=1; }
    # the Pocket layout is single-screen (one panel): only the "top" canvas has anything on it
    for s in home home2 lib lib2 launch; do
        f=$OUT/$size-layout-$s-top.png
        [ -s "$f" ] || { echo "FAIL $size: no $f"; fail=1; continue; }
        [ "$(convert "$f" -format '%[fx:standard_deviation>0.01]' info:)" = 1 ] || { echo "FAIL $size: $f is blank"; fail=1; }
    done
    grep 'bench:' "$log" | sed "s/^banana-ui: /$size /"
    grep 'boot-metrics:' "$log" | sed "s/^banana-ui: /$size /"
done
[ $fail = 0 ] && echo "banana-ui: all checks passed ($OUT)"
exit $fail

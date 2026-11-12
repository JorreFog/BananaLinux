# The AYN Thor's first-time setup, its accounts, its quick menu and its theme's fades (sourced by run.sh once: $OUT,
# $HERE and $fail are its). At the Thor's screens (1920x1080 top, 1240x1080 bottom) with BANANA_BOARD=thor and its
# default theme, Thor OLED; stand-ins for the Steam tool (fake-steam.sh), the home server (fake-sync.sh), the handheld
# tool's capabilities (a fan and stick lights) and Wi-Fi. The PNGs are $OUT/thor-st-*.png
TS=$OUT/thor-setup
rm -rf "$TS"; "$HERE/tests/fixture.sh" "$TS" >/dev/null
mkdir -p "$TS/run" "$TS/ds"
rm -f "$TS/data/setup-done" "$TS/data/setup-step" "$TS/data/accounts.conf" "$TS/data/wifi.conf"
[ ! -f "$TS/data/settings.conf" ] || sed -i '/^theme *=/d' "$TS/data/settings.conf"
printf 'wifi = on\n' >> "$TS/data/settings.conf"
printf '#!/bin/sh\n[ "$1" = caps ] && echo "fan leds gpuboost"\nexit 0\n' > "$TS/handheld"; chmod +x "$TS/handheld"
# ts_run <name> <script>: the log in $OUT/thor-st-<name>.log
ts_run() {
    n=$1; scr=$2; shift 2
    env BANANA_BOARD=thor BANANA_THEMES="$HERE/../system/rootfs/usr/share/banana/themes" BANANA_WIFI="$HERE/tests/fake-wifi.sh" \
        FAKE_WIFI_CONF="$TS/data/wifi.conf" BANANA_RUN="$TS/run" BANANA_DS_ROOT="$TS/ds" BANANA_SCRAPE=/bin/true BANANA_RA_CHECK=0 \
        BANANA_HANDHELD="$TS/handheld" BANANA_STEAM="$HERE/tests/fake-steam.sh" BANANA_SYNC="$HERE/tests/fake-sync.sh" FAKE_SYNC_LOG="$TS/sync.log" "$@" \
        "$HERE/banana-ui" --headless "$OUT/thor-st-" --size 1440x1080 --screens 1920x1080,1240x1080 --assets "$HERE/assets/rnds" \
        --splash "$HERE/assets/splash" --banana "$HERE/assets/banana" --systems "$HERE/../system/rootfs/etc/banana/systems.conf" \
        --roms "$TS/roms" --data "$TS/data" --script "$scr" 2> "$OUT/thor-st-$n.log" || { echo "FAIL thor setup: banana-ui ($n) exited $?"; fail=1; }
}
ts_check() { grep -q -- "$2" "$OUT/thor-st-$1.log" || { echo "FAIL thor setup ($1): no '$2'"; fail=1; }; }
ts_not() { if grep -q -- "$2" "$OUT/thor-st-$1.log"; then echo "FAIL thor setup ($1): '$2'"; fail=1; fi; }

# the slides: the RG DS's, and Controls (the fan, the lights), Home server and Steam; ten steps. Controls' rows change
# the settings themselves (the fan one to the right); Home server pairs with the stand-in (code 1)
ts_run slides "home 0; setup; setupstate; shot lang; setupat controls; setupstate; shot controls; press down; press right; setupat homeserver; setupstate; shot home; \
    press a; sleep 50; type 1; press start; sleep 300; wait 2500; setupstate; shot home-paired"
ts_check slides "setupstate: slide=language .* steps=10"
ts_check slides "setupstate: slide=controls pick=0 choices=Next|Fan|Stick lights|Light brightness "
ts_check slides "setupstate: slide=homeserver pick=0 choices=Pair|Skip for now "
ts_check slides "setupstate: slide=homeserver pick=0 choices=Next|Pair with another server "
grep -q '^fan = ' "$TS/data/settings.conf" || { echo "FAIL thor setup: the Controls slide's fan wasn't saved"; fail=1; }
grep -q '"token": "rh1_test"' "$TS/data/romsync.json" 2>/dev/null || { echo "FAIL thor setup: the Home server slide didn't pair"; fail=1; }
# Steam: the password straight through (logged in, the library read: 3 games), never in the tool's log
ts_run steam "home 0; setupat steam; setupstate; shot steam; press a; type joar; press start; type right; press start; sleep 300; sleep 300; sleep 300; setupstate; \
    sleep 300; sleep 300; shot steam-ok; setupstate"
ts_check steam "setupstate: slide=steam pick=0 choices=Log in|Skip for now .*steam=0"
ts_check steam "setupstate: slide=steam pick=0 choices=Next|Log in with another account .*steam=1 steamuser=joar "
grep -q '^login joar$' "$TS/run/fake-steam.log" || { echo "FAIL thor setup: no steam login: $(cat "$TS/run/fake-steam.log")"; fail=1; }
grep -q '^library' "$TS/run/fake-steam.log" || { echo "FAIL thor setup: the library wasn't read after the login"; fail=1; }
if grep -q 'right' "$TS/run/fake-steam.log"; then echo "FAIL thor setup: the Steam password in the tool's arguments"; fail=1; fi
# Steam Guard: the app's code asked for (the entry opens by itself), a wrong one asked again, the right one logs in
rm -f "$TS/run/fake-steam.user"
ts_run guard "home 0; setupat steam; press a; type joar; press start; type guard; press start; sleep 300; sleep 300; setupstate; shot steam-code; \
    type 11111; press start; sleep 300; sleep 300; setupstate; type 12345; press start; sleep 300; sleep 300; sleep 300; setupstate"
ts_check guard "setupstate: closed page=2"
ts_check guard "steam=1 steamuser=joar steamstate=ok joar"
grep -q '^code 11111$' "$TS/run/fake-steam.log" && grep -q '^code 12345$' "$TS/run/fake-steam.log" || { echo "FAIL thor setup: the Steam Guard codes weren't handed on"; fail=1; }
# the approval on the phone (nothing typed), then a wrong password: Try again
rm -f "$TS/run/fake-steam.user"
ts_run phone "home 0; setupat steam; press a; type joar; press start; type phone; press start; sleep 300; setupstate; shot steam-confirm; sleep 300; sleep 300; sleep 300; setupstate"
ts_check phone "steamstate=confirm"
ts_check phone "steam=1 steamuser=joar"
rm -f "$TS/run/fake-steam.user"
ts_run wrong "home 0; setupat steam; press a; type joar; press start; type nope; press start; sleep 300; sleep 300; setupstate; shot steam-wrong"
ts_check wrong "setupstate: slide=steam pick=0 choices=Try again|Skip for now .*steam=2"
# the summary lists the new steps
ts_run done "home 0; setupat done; setupstate; shot done"
ts_check done "setupstate: slide=done pick=0 choices=Finish"
# Settings > System > Accounts: Steam logged in, then out (A twice); SteamGridDB's key kept as the other accounts are
ts_run accounts "home 0; section Accounts; press down; press a; type joar; press start; type right; press start; sleep 300; sleep 300; sleep 300; shot accounts; \
    press a; press a; press down; press a; type KEY123; press start"
grep -q '^logout' "$TS/run/fake-steam.log" || { echo "FAIL thor accounts: Steam wasn't logged out"; fail=1; }
grep -q '^sgdb_key=KEY123$' "$TS/data/accounts.conf" || { echo "FAIL thor accounts: no sgdb_key in accounts.conf"; fail=1; }
# the quick menu (Home held in a game): its rows on the bottom screen, a step right on Performance kept for the game, a
# tap on the top brightness's slider, B closes it
ts_run quick "home 0; quick $TS/roms/nds/Peel Quest.nds; quickstate; shot quick; press down; press down; press down; press right; quickstate; touch 1 400 60; quickstate; press b; quickstate"
ts_check quick "quickstate: sel=0 top=[0-9]* bottom=[0-9]* volume=[0-9]* perf= fan=[a-z]* leds=[a-z]* leds_level=[a-z]* leds_effect=[a-z]*"
ts_check quick "quickstate: sel=3 .* perf=powersave"
ts_check quick "quickstate: sel=0 top=400 "
ts_check quick "quickstate: closed"
grep -q '^Peel Quest.nds = powersave$' "$TS/data/per-game-perf.conf" 2>/dev/null || { echo "FAIL thor quick menu: the game's performance mode wasn't kept"; fail=1; }
# the theme's fades (no iris at these shapes): a game ending (black to the menu), the boot into the menu, the arrival,
# the menu to the shutdown; each frame drawn at the screens' sizes, the first and last as they should be
ts_run fades "home 0; wait 2400; trans game fade-game-0 0; trans game fade-game-110 110; trans game fade-game-220 220; trans boot fade-boot-130 130; \
    trans arrival fade-arrival-420 420; trans arrival fade-arrival-1900 1900; trans outro fade-outro-200 200"
ts_check fades "trans game fade-game-0 at 0 ms (fades 1)"
if command -v convert >/dev/null; then
    for f in game-0 arrival-1900 outro-200; do
        [ "$(convert "$OUT/thor-st-fade-$f-top.png" -format '%[fx:maxima]' info: 2>/dev/null)" = 0 ] || { echo "FAIL thor fades: $f isn't black"; fail=1; }
    done
    for f in game-110 game-220 boot-130 arrival-420; do
        [ "$(identify -format '%wx%h' "$OUT/thor-st-fade-$f-top.png" 2>/dev/null)" = 1920x1080 ] || { echo "FAIL thor fades: $f's top isn't 1920x1080"; fail=1; }
        [ "$(convert "$OUT/thor-st-fade-$f-top.png" -format '%[fx:standard_deviation>0.01]' info: 2>/dev/null)" = 1 ] || { echo "FAIL thor fades: $f is blank"; fail=1; }
    done
    cmp -s "$OUT/thor-st-fade-game-110-top.png" "$OUT/thor-st-fade-game-220-top.png" && { echo "FAIL thor fades: the game's fade doesn't move"; fail=1; }
    # the setup at the Thor's shapes: true black, the status bar over the top screen (lit pixels in its first 90 lines),
    # every slide drawn
    for s in lang controls home home-paired steam steam-ok steam-confirm steam-wrong done quick; do
        [ "$(convert "$OUT/thor-st-$s-bot.png" -format '%[fx:standard_deviation>0.01]' info: 2>/dev/null)" = 1 ] || { echo "FAIL thor setup: $s's bottom is blank"; fail=1; }
    done
    [ "$(convert "$OUT/thor-st-lang-top.png" -format '%[pixel:p{4,1075}]' info: 2>/dev/null)" = "srgb(0,0,0)" ] || { echo "FAIL thor setup: the top isn't true black"; fail=1; }
    [ "$(convert "$OUT/thor-st-lang-top.png" -crop 1920x90+0+0 -format '%[fx:maxima>0.5]' info: 2>/dev/null)" = 1 ] || { echo "FAIL thor setup: no status bar on the setup's top screen"; fail=1; }
fi

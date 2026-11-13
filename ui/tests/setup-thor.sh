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
# the pairing code's keyboard left with B (nothing typed): not pairing any more, the slide offers Pair again and A
# starts it again (the keyboard is back); unpaired for this run, the pairing above put back after it
mv "$TS/data/romsync.json" "$TS/romsync.kept" 2>/dev/null
ts_run homeback "home 0; setupat homeserver; press a; sleep 50; press b; setupstate; press a; sleep 50; setupstate"
ts_check homeback "setupstate: slide=homeserver pick=0 choices=Pair|Skip for now "
ts_check homeback "setupstate: closed page="
if [ -f "$TS/data/romsync.json" ]; then echo "FAIL thor setup: B on the pairing code paired"; fail=1; fi
mv "$TS/romsync.kept" "$TS/data/romsync.json" 2>/dev/null
# the RG DS's setup is not the Thor's: no Steam step though its tool is in every image's files (no stand-in named:
# the real one, by BANANA_LIBEXEC), and no Home server step, on the universal card either
for b in rgds universal; do
    ts_run "board-$b" "home 0; setup; setupstate; setupat steam; setupstate; setupat homeserver; setupstate" BANANA_BOARD=$b BANANA_STEAM= \
        BANANA_LIBEXEC="$HERE/../system/rootfs/usr/libexec/banana"
    ts_check "board-$b" "setupstate: slide=language .* steps=8"
    ts_not "board-$b" "slide=steam"
    ts_not "board-$b" "slide=homeserver"
    ts_not "board-$b" " steam="
done
# Steam: the password straight through (logged in, the library read: 3 games), never in the tool's log
ts_run steam "home 0; setupat steam; setupstate; shot steam; press down; press a; type joar; press start; type right; press start; sleep 300; sleep 300; sleep 300; setupstate; \
    sleep 300; sleep 300; shot steam-ok; setupstate"
ts_check steam "setupstate: slide=steam pick=0 choices=Sign in with the Steam app|Type the name and password|Skip for now .*steam=0"
ts_check steam "setupstate: slide=steam pick=0 choices=Next|Log in with another account .*steam=1 steamuser=joar "
grep -q '^login joar$' "$TS/run/fake-steam.log" || { echo "FAIL thor setup: no steam login: $(cat "$TS/run/fake-steam.log")"; fail=1; }
grep -q '^library' "$TS/run/fake-steam.log" || { echo "FAIL thor setup: the library wasn't read after the login"; fail=1; }
if grep -q 'right' "$TS/run/fake-steam.log"; then echo "FAIL thor setup: the Steam password in the tool's arguments"; fail=1; fi
# Steam Guard: the app's code asked for (the entry opens by itself), a wrong one asked again, the right one logs in
rm -f "$TS/run/fake-steam.user"
ts_run guard "home 0; setupat steam; press down; press a; type joar; press start; type guard; press start; sleep 300; sleep 300; setupstate; shot steam-code; \
    type 11111; press start; sleep 300; sleep 300; setupstate; type 12345; press start; sleep 300; sleep 300; sleep 300; setupstate"
ts_check guard "setupstate: closed page=2"
ts_check guard "steam=1 steamuser=joar steamstate=ok joar"
grep -q '^code 11111$' "$TS/run/fake-steam.log" && grep -q '^code 12345$' "$TS/run/fake-steam.log" || { echo "FAIL thor setup: the Steam Guard codes weren't handed on"; fail=1; }
# the approval on the phone (nothing typed), then a wrong password: Try again
rm -f "$TS/run/fake-steam.user"
ts_run phone "home 0; setupat steam; press down; press a; type joar; press start; type phone; press start; sleep 300; setupstate; shot steam-confirm; sleep 300; sleep 300; sleep 300; setupstate"
ts_check phone "steamstate=confirm"
ts_check phone "steam=1 steamuser=joar"
rm -f "$TS/run/fake-steam.user"
ts_run wrong "home 0; setupat steam; press down; press a; type joar; press start; type nope; press start; sleep 300; sleep 300; setupstate; shot steam-wrong"
ts_check wrong "setupstate: slide=steam pick=0 choices=Try again|Sign in with the Steam app|Skip for now .*steam=2"
# the sign-in with the Steam app: its code (the stand-in's 25 x 25) drawn on the top screen in whole px a module, a new
# code (qr 2) read and drawn again, then ok as the account the app approved; B while it waits stops it (steam cancel)
# and the slide's choices come back; a sign-in that fails offers Try again and the password's way
rm -f "$TS/run/fake-steam.user"
st_q="home 0; setupat steam; press a; sleep 300; sleep 300; sleep 300; shot steam-qr; setupstate"
for i in 1 2 3 4 5 6 7 8; do st_q="$st_q; sleep 300; setupstate"; done
ts_run qr "$st_q; shot steam-qr-later; sleep 300; sleep 300; sleep 300; sleep 300; sleep 300; sleep 300; setupstate" FAKE_STEAM_QR_ROUNDS=3
ts_check qr "setupstate: slide=steam pick=0 choices=Waiting for the scan|Type the name and password instead|Skip for now .*steamstate=qr 1 qr=25x[0-9]* qrn=1"
ts_check qr "steamstate=qr 2 qr=25x[0-9]* qrn=2"
ts_check qr "steam=1 steamuser=scanned steamstate=ok scanned"
k=$(grep -o 'qr=25x[0-9]*' "$OUT/thor-st-qr.log" | grep -v 'x0$' | head -n 1 | cut -dx -f2)
[ -n "$k" ] && [ "$k" -ge 16 ] && [ "$k" -le 22 ] || { echo "FAIL thor setup: the code's modules are ${k:-?} px (16..22 for 400-560 px)"; fail=1; }
grep -q '^login-qr' "$TS/run/fake-steam.log" || { echo "FAIL thor setup: steam login-qr wasn't run"; fail=1; }
# (the new code is the one read: its dark modules aren't the first's)
d1=$(grep -o 'qrn=1 qrdark=[0-9]*' "$OUT/thor-st-qr.log" | head -n 1 | cut -d= -f3); d2=$(grep -o 'qrn=2 qrdark=[0-9]*' "$OUT/thor-st-qr.log" | head -n 1 | cut -d= -f3)
[ -n "$d1" ] && [ -n "$d2" ] && [ "$d1" != "$d2" ] || { echo "FAIL thor setup: the new code wasn't read (${d1:-?} / ${d2:-?} dark modules)"; fail=1; }
if command -v convert >/dev/null; then
    # the card: its light ground (Thor OLED's ink, #EFE9DE: the material look's code is on the ink, not on white) and
    # pure black right of the text column, both a good part of it
    hist=$(convert "$OUT/thor-st-steam-qr-top.png" -crop 600x900+1320+90 +repage -format %c histogram:info: 2>/dev/null)
    wpx=$(echo "$hist" | grep -i '#EFE9DE' | awk '{ print $1 }' | tr -d ':'); bpx=$(echo "$hist" | grep -i '#000000 ' | awk '{ print $1 }' | tr -d ':')
    [ "${wpx:-0}" -gt 60000 ] && [ "${bpx:-0}" -gt 40000 ] || { echo "FAIL thor setup: no code drawn on the top screen (light ${wpx:-0}, black ${bpx:-0})"; fail=1; }
fi
rm -f "$TS/run/fake-steam.user"
ts_run qrcancel "home 0; setupat steam; press a; sleep 300; sleep 300; sleep 300; press b; sleep 300; sleep 300; sleep 300; setupstate"
ts_check qrcancel "setupstate: slide=steam pick=0 choices=Sign in with the Steam app|Type the name and password|Skip for now .*steam=0"
grep -q '^cancel' "$TS/run/fake-steam.log" || { echo "FAIL thor setup: B didn't stop the sign-in (steam cancel)"; fail=1; }
# B while the downloader is still being fetched (the first sign-in: the tool's cancel has nothing to stop yet): the
# sign-in itself is ended, the choices come back, and it doesn't go on to show a code later
ts_run qrfetch "home 0; setupat steam; press a; sleep 300; setupstate; press b; sleep 300; sleep 300; setupstate" FAKE_STEAM_FETCH=5
ts_check qrfetch "setupstate: slide=steam pick=0 choices=Asking Steam for a code|"
ts_check qrfetch "setupstate: slide=steam pick=0 choices=Sign in with the Steam app|Type the name and password|Skip for now .*steam=0"
# (gone, or ended and not collected yet: Z)
case $(awk '{ print $3 }' "/proc/$(cat "$TS/run/fake-steam.fetching" 2>/dev/null)/stat" 2>/dev/null) in
""|Z) ;;
*) echo "FAIL thor setup: B left the sign-in running while it fetched"; fail=1 ;;
esac
rm -f "$TS/run/fake-steam.fetching" "$TS/run/steam.pid" "$TS/run/fake-steam.cancel"
ts_run qrfail "home 0; setupat steam; press a; sleep 300; sleep 300; sleep 300; sleep 300; sleep 300; sleep 300; sleep 300; sleep 300; setupstate; shot steam-qr-failed" FAKE_STEAM_QR=fail FAKE_STEAM_QR_ROUNDS=1
ts_check qrfail "setupstate: slide=steam pick=0 choices=Try again|Type the name and password|Skip for now .*steam=2"
# the summary lists the new steps
ts_run done "home 0; setupat done; setupstate; shot done"
ts_check done "setupstate: slide=done pick=0 choices=Finish"
# Settings > System > Accounts: Steam logged in, then out (A twice); SteamGridDB's key kept as the other accounts are
ts_run accounts "home 0; section Accounts; press down; press a; sleep 300; sleep 300; shot accounts-qr; press a; type joar; press start; type right; press start; sleep 300; sleep 300; sleep 300; shot accounts; \
    press a; press a; press down; press a; type KEY123; press start"
grep -q '^logout' "$TS/run/fake-steam.log" || { echo "FAIL thor accounts: Steam wasn't logged out"; fail=1; }
[ "$(grep -c '^login-qr' "$TS/run/fake-steam.log")" -ge 2 ] || { echo "FAIL thor accounts: A on Steam didn't start the sign-in with the app"; fail=1; }
grep -q '^sgdb_key=KEY123$' "$TS/data/accounts.conf" || { echo "FAIL thor accounts: no sgdb_key in accounts.conf"; fail=1; }
# the quick menu (Home held in a game), a board of tiles on the bottom screen: the sliders one under the other, then the
# choices three a row (down from the last slider is the game's mode, right of it the fan); A steps a choice and the
# game's mode is kept for the game; a tap on a slider sets its level; the meters show what the sysstat tool says; B closes
printf '#!/bin/sh\nprintf "cpu=20\\ncpu_mhz=2710\\ncpu_c=50\\ngpu=4\\ngpu_mhz=680\\ngpu_c=44\\nmem_used_mb=2048\\nmem_total_mb=7300\\nmem_pct=28\\npower_mw=3300\\npower_state=Charging\\nbattery=100\\nfan_pct=16\\n"\n' > "$TS/sysstat"; chmod +x "$TS/sysstat"
ts_run quick "home 0; quick $TS/roms/nds/Peel Quest.nds; quickstate; shot quick; press down; press down; press down; press a; quickstate; press right; press a; quickstate; press up; quickstate; touch 1 320 130; quickstate; press b; quickstate" BANANA_SYSSTAT="$TS/sysstat"
ts_check quick "quickstate: sel=0 top=[0-9]* bottom=[0-9]* volume=[0-9]* perf= fan=[a-z]* leds=[a-z]* leds_level=[a-z]* leds_effect=[a-z]*"
ts_check quick "quickstate: sel=3 .* perf=powersave fan=moderate"
ts_check quick "quickstate: sel=4 .* perf=powersave fan=a"
ts_check quick "quickstate: sel=2 "
ts_check quick "quickstate: sel=2 top=[0-9]* bottom=[0-9]* volume=60 "
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
    for s in lang controls home home-paired steam steam-ok steam-confirm steam-wrong steam-qr steam-qr-failed accounts-qr done quick; do
        [ "$(convert "$OUT/thor-st-$s-bot.png" -format '%[fx:standard_deviation>0.01]' info: 2>/dev/null)" = 1 ] || { echo "FAIL thor setup: $s's bottom is blank"; fail=1; }
    done
    [ "$(convert "$OUT/thor-st-lang-top.png" -format '%[pixel:p{4,1075}]' info: 2>/dev/null)" = "srgb(0,0,0)" ] || { echo "FAIL thor setup: the top isn't true black"; fail=1; }
    [ "$(convert "$OUT/thor-st-lang-top.png" -crop 1920x90+0+0 -format '%[fx:maxima>0.5]' info: 2>/dev/null)" = 1 ] || { echo "FAIL thor setup: no status bar on the setup's top screen"; fail=1; }
fi

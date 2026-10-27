# The first-time setup and the first boot's arrival (sourced by run.sh once per panel size: $size, $OUT, $HERE and
# $fail are its). The setup's slides one at a time with stand-ins for Wi-Fi (fake-wifi.sh), the RetroAchievements
# site (fake-http.py's login), the emulators' settings (BANANA_DS_ROOT) and the scraper; a shot of every slide.
SFX=$OUT/setup-fixture-$size
rm -rf "$SFX"; "$HERE/tests/fixture.sh" "$SFX" >/dev/null
mkdir -p "$SFX/ds" "$SFX/run" "$SFX/web"
rm -f "$SFX/data/setup-done" "$SFX/data/setup-step" "$SFX/data/accounts.conf" "$SFX/data/wifi.conf"
python3 -I "$HERE/tests/fake-http.py" "$SFX/web" "$SFX/port" &
st_http=$!
for i in $(seq 50); do [ -s "$SFX/port" ] && break; sleep 0.1; done
st_ra="http://127.0.0.1:$(head -n 1 "$SFX/port")/dorequest.php"
# st_run <name> <script> [env...]: the log in $OUT/$size-st-<name>.log
st_run() {
    n=$1; scr=$2; shift 2
    env BANANA_WIFI="$HERE/tests/fake-wifi.sh" FAKE_WIFI_CONF="$SFX/data/wifi.conf" BANANA_RUN="$SFX/run" BANANA_DS_ROOT="$SFX/ds" \
        BANANA_SCRAPE=/bin/true BANANA_RA_URL="$st_ra" "$@" "$HERE/banana-ui" --headless "$OUT/$size-" --size $size \
        --assets "$HERE/assets/rnds" --splash "$HERE/assets/splash" --banana "$HERE/assets/banana" \
        --systems "$HERE/../system/rootfs/etc/banana/systems.conf" --roms "$SFX/roms" --data "$SFX/data" \
        --script "$scr" 2> "$OUT/$size-st-$n.log" || { echo "FAIL $size: banana-ui (setup $n) exited $?"; fail=1; }
}
st_check() { grep -q -- "$2" "$OUT/$size-st-$1.log" || { echo "FAIL $size (setup $1): no '$2'"; fail=1; }; }
st_not() { if grep -q -- "$2" "$OUT/$size-st-$1.log"; then echo "FAIL $size (setup $1): '$2'"; fail=1; fi; }

# the whole setup, as on a first boot: Language (kept), Wi-Fi (Banana Home joined with its password, then tested),
# Time zone (one to the right), DS name (typed), RetroAchievements (logged in, checked with the stand-in site), Box art
# (started), the summary, Finish: the host's Welcome
st_run flow "home 0; setup; setupstate; shot st-lang; press down; press a; shot st-wifi; setupstate; press a; sleep 50; press a; type password1; press start; \
    shot st-wifi-joining; wait 2500; sleep 50; sleep 50; setupstate; shot st-wifi-ok; press a; shot st-tz; press right; press down; press a; setupstate; \
    shot st-name; press a; type Joar; press start; shot st-name-set; press a; shot st-ra; press a; type good; press start; type right; press start; \
    sleep 300; sleep 300; sleep 300; sleep 300; sleep 300; setupstate; shot st-ra-ok; press a; shot st-art; press a; press start; setupstate; shot st-done; \
    press a; card st-welcome Welcome"
st_check flow "setupstate: slide=language pick=0 choices=Language|Next"
st_check flow "setupstate: slide=wifi pick=0 choices=Choose a network|Skip for now link=-1"
st_check flow "setupstate: slide=wifi pick=0 choices=Next|Test again|Choose another network link=1 net=1 time=1"
st_check flow "setupstate: slide=name"
st_check flow "setupstate: slide=retroachievements pick=0 choices=Next|Log in with another account .* ra=1"
st_check flow "setupstate: slide=done pick=0 choices=Finish"
st_check flow "setup finished: the welcome"
[ -f "$SFX/data/setup-done" ] || { echo "FAIL $size: the setup's Finish didn't write setup-done"; fail=1; }
if [ -f "$SFX/data/setup-step" ]; then echo "FAIL $size: setup-step left after Finish"; fail=1; fi
grep -q 'ssid="Banana Home"' "$SFX/data/wifi.conf" || { echo "FAIL $size: the network joined isn't in wifi.conf"; fail=1; }
grep -q '^ra_user=good$' "$SFX/data/accounts.conf" || { echo "FAIL $size: accounts.conf: $(cut -d= -f1 "$SFX/data/accounts.conf" | tr '\n' ' ')"; fail=1; }
grep -q '^firmware.nickname = Joar$' "$SFX/ds/bananads/bananads.conf" || { echo "FAIL $size: the DS name: $(cat "$SFX/ds/bananads/bananads.conf" 2>/dev/null)"; fail=1; }
grep -q '^timezone = [^U]' "$SFX/data/settings.conf" || { echo "FAIL $size: the setup's time zone wasn't saved"; fail=1; }
grep -q 'POST /dorequest.php good' "$SFX/port.log" || { echo "FAIL $size: the login wasn't checked with the site"; fail=1; }
if grep -q 'right' "$SFX/port.log"; then echo "FAIL $size: a password in the stand-in's log"; fail=1; fi

# what goes wrong: a network with no internet (the test says so, Test again first); a wrong password and the site
# down (the login checked: refused / no answer; the saved account checked again without typing it); no Wi-Fi at all, every step skipped (START), the
# summary saying so
rm -f "$SFX/data/setup-done" "$SFX/data/accounts.conf"
st_run nonet "home 0; setupat wifi; sleep 50; setupstate; shot st-wifi-nonet" FAKE_WIFI_NET=fail FAKE_WIFI_TIME=fail
st_check nonet "setupstate: slide=wifi pick=0 choices=Test again|Choose another network|Skip for now link=1 net=0 time=0"
st_run rawrong "home 0; setupat retroachievements; press a; type good; press start; type wrong; press start; sleep 300; sleep 300; sleep 300; sleep 300; sleep 300; setupstate; shot st-ra-wrong"
st_check rawrong "setupstate: slide=retroachievements pick=0 choices=Try again|Skip for now .* ra=2"
st_run radown "home 0; setupat retroachievements; press a; sleep 300; sleep 300; sleep 300; sleep 300; sleep 300; setupstate; shot st-ra-down" BANANA_RA_URL=http://127.0.0.1:9/dorequest.php
st_check radown "setupstate: slide=retroachievements pick=0 choices=Check again|Log in with another account|Skip for now .* ra=3"
rm -f "$SFX/data/wifi.conf" "$SFX/data/accounts.conf"
st_run skip "home 0; setupat wifi; setupstate; press start; press start; press start; press start; press start; setupstate; shot st-done-skipped; press start"
st_check skip "setupstate: slide=wifi pick=0 choices=Choose a network|Skip for now"
st_check skip "setupstate: slide=done"
st_check skip "setup finished: the welcome"

# back on the slide reached: setup-step says where a setup cut off (a restart) was; B goes back a slide; the first slide
# has no way back at the boot
rm -f "$SFX/data/setup-done"
st_run resume "home 0; setup; press down; press a; press a; setupstate; press b; press b; press b; setupstate"
[ "$(cat "$SFX/data/setup-step" 2>/dev/null)" = "language boot" ] || { echo "FAIL $size: setup-step: $(cat "$SFX/data/setup-step" 2>/dev/null)"; fail=1; }
st_check resume "setupstate: slide=language"
# touch: the time zone's right half moves it on, a tap on Next goes on
st_run touch "home 0; setupat timezone; touch 1 500 111; touch 1 320 177; setupstate"
st_check touch "setupstate: slide=name"
# the setup again from Settings > System: B on its first slide goes back to Settings, Finish too (no Welcome)
st_run again "home 0; setupagain; press b; setupstate; setupagain; press start; press start; press start; press start; press start; press start; press a; setupstate"
st_check again "setupstate: closed page=0 open=1"
st_not again "setup finished"
# the language picked on the first slide: saved, the menu starts again (LANGUAGE), on the Wi-Fi slide (setup-step)
st_run lang "home 0; setup; press right; press down; press a"
st_check lang "press a: settings result 9"
grep -q '^language = zh-cn' "$SFX/data/settings.conf" || { echo "FAIL $size: the setup's language wasn't saved"; fail=1; }
[ "$(cat "$SFX/data/setup-step" 2>/dev/null)" = "wifi boot" ] || { echo "FAIL $size: setup-step after the language: $(cat "$SFX/data/setup-step" 2>/dev/null)"; fail=1; }
st_run zh "home 0; setupat wifi; shot st-zh-wifi"
sed -i '/^language = /d' "$SFX/data/settings.conf"

# the arrival: its frames drawn (the zoom, the rumble, the burst, the flight, the glow, white, black, the card, dark)
st_run arrival "arrival ar-zoom 400; arrival ar-rumble 900; arrival ar-burst 1300; arrival ar-fly 2600; arrival ar-glow 4100; arrival ar-white 4500; \
    arrival ar-black 5000; arrival ar-card 7000; arrival ar-end 8650; arrivalbench"
st_check arrival "arrival cue burst"
st_check arrival "arrival cue flash"
for s in ar-zoom ar-rumble ar-burst ar-fly ar-glow; do
    for p in top bot; do
        [ "$(convert "$OUT/$size-$s-$p.png" -format '%[fx:standard_deviation>0.01]' info: 2>/dev/null)" = 1 ] || { echo "FAIL $size: the arrival's $s ($p) is blank"; fail=1; }
    done
done
[ "$(convert "$OUT/$size-ar-white-top.png" -format '%[fx:minima]' info: 2>/dev/null)" = 1 ] || { echo "FAIL $size: the arrival's white isn't white"; fail=1; }
for s in ar-black ar-end; do
    [ "$(convert "$OUT/$size-$s-top.png" -format '%[fx:maxima]' info: 2>/dev/null)" = 0 ] || { echo "FAIL $size: the arrival's $s isn't black"; fail=1; }
done
[ "$(convert "$OUT/$size-ar-card-top.png" -format '%[fx:standard_deviation>0.01]' info: 2>/dev/null)" = 1 ] || { echo "FAIL $size: the arrival's card is blank"; fail=1; }
# every slide's shot is there and drawn
for s in st-lang st-wifi st-wifi-joining st-wifi-ok st-tz st-name st-name-set st-ra st-ra-ok st-art st-done st-welcome st-wifi-nonet st-ra-wrong st-ra-down st-done-skipped st-zh-wifi; do
    [ "$(convert "$OUT/$size-$s-top.png" -format '%[fx:standard_deviation>0.01]' info: 2>/dev/null)" = 1 ] || { echo "FAIL $size: the setup's $s shot is missing or blank"; fail=1; }
done
kill $st_http 2>/dev/null || true
grep 'arrivalbench' "$OUT/$size-st-arrival.log" | sed "s/^banana-ui: /$size /"

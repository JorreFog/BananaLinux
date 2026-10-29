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
# the systems list: the card's systems.conf merged over the image's per system (systems-merge), and the history of old
# defaults it uses up to date with git (tools/systems-history.sh)
sh "$HERE/tests/systems-merge.sh" || fail=1
sh "$HERE/../tools/systems-history.sh" --check || fail=1
# the Pokémon companion's decoder (on data made from the documented layouts), and the made-up game and replies it
# leaves in $OUT for the game panel's run below
make -s -C "$HERE" tests/pokemon-test >/dev/null && "$HERE/tests/pokemon-test" "$OUT" 2> "$OUT/pokemon-test.log" || { echo "FAIL the Pokémon companion's decoder: $(grep FAIL "$OUT/pokemon-test.log" | head -3)"; fail=1; }
# the game companion's engine (matching, reads, values), and the made-up ROMs and replies for the game panel's run below
make -s -C "$HERE" tests/gamecomp-test >/dev/null && "$HERE/tests/gamecomp-test" "$OUT" "$HERE/tests/companions" "$HERE/../system/rootfs/usr/share/banana/companions" 2> "$OUT/gamecomp-test.log" || { echo "FAIL the game companion's engine: $(grep FAIL "$OUT/gamecomp-test.log" | head -3)"; fail=1; }
# the left stick as a d-pad: its deadzone and hysteresis
make -s -C "$HERE" tests/input-test >/dev/null && "$HERE/tests/input-test" 2> "$OUT/input-test.log" || { echo "FAIL the stick: $(grep FAIL "$OUT/input-test.log" | head -3)"; fail=1; }
# every icon the theme's systems.cfg names is there (Poketool's tile pointed at the old Bank & Trade logo)
awk '!/^#/ && NF >= 3 && $2 != "-" { print $2 }' "$HERE/assets/rnds/systems.cfg" | while read -r ic; do
    [ -f "$HERE/assets/rnds/icons/$ic" ] || { echo "FAIL systems.cfg: no icons/$ic"; exit 1; }
done || fail=1
grep -q '^rocknixdsbank .* poketool\.png ' "$HERE/assets/rnds/systems.cfg" || { echo "FAIL systems.cfg: Poketool's tile isn't its own logo"; fail=1; }
for size in 640x480 1024x768; do
    log=$OUT/$size.log
    "$HERE/banana-ui" --headless "$OUT/$size-" --size $size --assets "$HERE/assets/rnds" \
        --splash "$HERE/assets/splash" --banana "$HERE/assets/banana" \
        --systems "$HERE/../system/rootfs/etc/banana/systems.conf" --roms "$FX/roms" --data "$FX/data" \
        --script "splash boot-drop 200; splash boot-word 700; splash boot-click 1100 1; home 0; wait 2400; shot home; bench 120; press a; wait 2400; shot lib; press right; wait 2400; shot lib2; bench 120; press a; wait 3000; shot ready; press b; press b; press start; shot settings; press a; press down; shot settings-list; press r; shot settings-page; press b; press r; press r; press r; press r; press r; press a; shot settings-system; press down; press right; shot settings-tz; press b; press b; romicon $FX/roms/nds/Yellow Planet [!].zip; romicon $FX/roms/nds/Peel Quest.nds; battery 50; battery 19; shot battery-low; press left; battery 18; battery 4; wait 2400; shot battery-empty; battery 4 charging; shot battery-charging; battery 19; \
            outro bye-start 0; outro bye-eject 330; outro bye-word 520; outro bye-fall 820; outro bye-crt 1030; outro bye-end 1300" \
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
    check "outro cue eject"                 # the shutdown animation: the cartridge popped out
    for s in bye-start bye-eject bye-word bye-fall bye-crt; do
        for p in top bot; do
            f=$OUT/$size-$s-$p.png
            [ -s "$f" ] || { echo "FAIL $size: no $f"; fail=1; continue; }
            [ "$(convert "$f" -format '%[fx:standard_deviation>0.01]' info:)" = 1 ] || { echo "FAIL $size: $f is blank"; fail=1; }
        done
    done
    for p in top bot; do     # the shutdown ends on black panels
        [ "$(convert "$OUT/$size-bye-end-$p.png" -format '%[fx:maxima]' info:)" = 0 ] || { echo "FAIL $size: the shutdown's last frame isn't black ($p)"; fail=1; }
    done
    [ "$(grep -c 'notice Battery low: 19%' "$log")" = 2 ] || { echo "FAIL $size: the notice didn't come back after charging"; fail=1; }
    grep -q "battery 18%" "$log" && { echo "FAIL $size: a second notice in the same step"; fail=1; }
    for s in boot-drop boot-word boot-click home lib lib2 ready settings settings-list settings-system settings-tz battery-low battery-empty battery-charging; do
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
    # a saved network (#40): A joins it with the password wifi.conf already has, no keyboard; its block moves first
    # with the highest priority, its psk as it was
    nlog=$OUT/$size-wifi-saved.log
    printf 'ctrl_interface=/run/wpa\nnetwork={\n\tssid="Cafe Free"\n\tkey_mgmt=NONE\n\tpriority=2\n}\nnetwork={\n\tssid="Banana Home"\n\tpsk="oldpassw"\n\tpriority=1\n}\n' > "$conf"
    BANANA_WIFI="$HERE/tests/fake-wifi.sh" FAKE_WIFI_CONF="$conf" "$HERE/banana-ui" --headless "$OUT/$size-" --size $size \
        --assets "$HERE/assets/rnds" --systems "$HERE/../system/rootfs/etc/banana/systems.conf" --roms "$FX/roms" --data "$FX/data" \
        --script "home 0; press start; press r; press r; press r; press r; press a; press down; press a; wait 2500; shot wifi-saved-list; press a; shot wifi-saved; wait 2500; shot wifi-saved-joined" 2> "$nlog" || { echo "FAIL $size: banana-ui (saved Wi-Fi) exited $?"; fail=1; }
    [ "$(sed -n 's/^[[:space:]]*ssid=//p' "$conf" | tr '\n' '|')" = '"Banana Home"|"Cafe Free"|' ] || { echo "FAIL $size: saved network not moved first: $(grep ssid= "$conf" | tr -d '\t' | tr '\n' ' ')"; fail=1; }
    grep -q 'psk="oldpassw"' "$conf" && grep -q 'priority=3' "$conf" && [ "$(grep -c priority= "$conf")" = 2 ] || { echo "FAIL $size: the saved block: $(tr -d '\t' < "$conf" | tr '\n' ' ')"; fail=1; }
    rm -f "$conf"
    # Bluetooth: Settings > Network > Bluetooth switched on (the script's up), Bluetooth devices: a search (a stand-in
    # bluetooth script: headphones, a pad, an 8BitDo that refuses), the headphones paired (and connected), forgotten,
    # the 8BitDo's refusal, the headphones paired again, then Bluetooth off (down); settings.conf keeps it off
    blog=$OUT/$size-bt.log; BT=$OUT/$size-bt; rm -rf "$BT"
    BANANA_BLUETOOTH="$HERE/tests/fake-bluetooth.sh" FAKE_BT="$BT" "$HERE/banana-ui" --headless "$OUT/$size-" --size $size \
        --assets "$HERE/assets/rnds" --systems "$HERE/../system/rootfs/etc/banana/systems.conf" --roms "$FX/roms" --data "$FX/data" \
        --script "home 0; press start; press r; press r; press r; press r; press a; press down; press down; shot bt-row; press right; wait 300; \
            press down; shot bt-devices-row; press a; shot bt-list; press down; press a; shot bt-paired; press y; shot bt-forgot; press up; press a; shot bt-refused; \
            press down; press a; press b; press up; press left; wait 300; shot bt-off; press down; press down; press right; wait 300; shot bt-audio" 2> "$blog" || { echo "FAIL $size: banana-ui (Bluetooth) exited $?"; fail=1; }
    [ "$(grep -v '^devices$' "$BT/log" 2>/dev/null | tr '\n' '|')" = "up|up|scan|pair AA:BB:CC:DD:EE:01|forget AA:BB:CC:DD:EE:01|pair AA:BB:CC:DD:EE:03|pair AA:BB:CC:DD:EE:01|down|audio|" ] \
        || { echo "FAIL $size: the bluetooth script's calls: $(tr '\n' '|' < "$BT/log" 2>/dev/null)"; fail=1; }
    [ "$(cat "$BT/paired" 2>/dev/null)" = AA:BB:CC:DD:EE:01 ] || { echo "FAIL $size: paired at the end: $(cat "$BT/paired" 2>/dev/null)"; fail=1; }
    grep -q '^bluetooth = off$' "$FX/data/settings.conf" && grep -q '^bt_audio = quality$' "$FX/data/settings.conf" || { echo "FAIL $size: settings.conf: $(grep -E 'bluetooth|bt_audio' "$FX/data/settings.conf" | tr '\n' ' ')"; fail=1; }
    sed -i '/^bt_audio = /d' "$FX/data/settings.conf"	# (the next size starts at the default again)
    for s in bt-row bt-devices-row bt-list bt-paired bt-forgot bt-refused bt-off bt-audio; do
        [ -s "$OUT/$size-$s-top.png" ] && [ -s "$OUT/$size-$s-bot.png" ] || { echo "FAIL $size: no $s shot"; fail=1; }
    done
    # the file sharing login (#24): Network > Sharing name and password, the keyboard on the name (banana), START, then
    # on the password (linux), START: both in accounts.conf, where the share script reads them
    alog=$OUT/$size-smb.log
    rm -f "$FX/data/accounts.conf"
    "$HERE/banana-ui" --headless "$OUT/$size-" --size $size --assets "$HERE/assets/rnds" --systems "$HERE/../system/rootfs/etc/banana/systems.conf" \
        --roms "$FX/roms" --data "$FX/data" --script "home 0; press start; press r; press r; press r; press r; press a; press up; press up; press up; press a; shot smb-name; \
            press b; press start; shot smb-pass; press start; wait 300; shot smb-done" 2> "$alog" || { echo "FAIL $size: banana-ui (sharing login) exited $?"; fail=1; }
    grep -qx 'smb_user=banan' "$FX/data/accounts.conf" && grep -qx 'smb_password=linux' "$FX/data/accounts.conf" || { echo "FAIL $size: accounts.conf: $(tr '\n' ' ' < "$FX/data/accounts.conf" 2>/dev/null)"; fail=1; }
    rm -f "$FX/data/accounts.conf"
    # Favorites starred itself (first on the shelf, ahead of every system): a GBA favourite launches with GBA's
    # emulator, not the shelf's one along (its systems' places moved when Favorites went in before them)
    flog=$OUT/$size-favorites.log
    cp "$FX/data/favorites.txt" "$OUT/favorites.txt.keep"
    echo "gba/Orchard Tactics.gba" > "$FX/data/favorites.txt"; echo favorites > "$FX/data/favorite-systems.txt"
    "$HERE/banana-ui" --headless "$OUT/$size-" --size $size --assets "$HERE/assets/rnds" \
        --systems "$HERE/../system/rootfs/etc/banana/systems.conf" --roms "$FX/roms" --data "$FX/data" \
        --script "lib 0 0; press a; wait 3000; wait 1" 2> "$flog" || { echo "FAIL $size: banana-ui (favorites) exited $?"; fail=1; }
    grep -q "launch 0: .*Orchard Tactics.gba (system gba)" "$flog" || { echo "FAIL $size: the favourite: $(grep launch "$flog")"; fail=1; }
    mv "$OUT/favorites.txt.keep" "$FX/data/favorites.txt"; rm -f "$FX/data/favorite-systems.txt"
    # a game's own name and sort title (#29): X on the last DS game (Yellow Planet), down to Sort title, "a" typed by
    # touch, START; up to Name, one letter deleted, START: both in game-titles.tsv, and the game first on the shelf
    tlog=$OUT/$size-titles.log; T="$FX/data/game-titles.tsv"; rm -f "$T"
    "$HERE/banana-ui" --headless "$OUT/$size-" --size $size --assets "$HERE/assets/rnds" --systems "$HERE/../system/rootfs/etc/banana/systems.conf" \
        --roms "$FX/roms" --data "$FX/data" --script "lib 0 4; press x; press down; press down; press down; press down; press down; press down; shot titles-page; press a; $(key 2,0) press start; \
            press up; press a; press b; press start; wait 300; shot titles-done" 2> "$tlog" || { echo "FAIL $size: banana-ui (titles) exited $?"; fail=1; }
    [ "$(cat "$T" 2>/dev/null)" = "$(printf 'nds/Yellow Planet [!].zip\tYellow Plane\ta')" ] || { echo "FAIL $size: game-titles.tsv: $(cat "$T" 2>/dev/null)"; fail=1; }
    "$HERE/banana-ui" --headless "$OUT/$size-" --size $size --assets "$HERE/assets/rnds" --systems "$HERE/../system/rootfs/etc/banana/systems.conf" \
        --roms "$FX/roms" --data "$FX/data" --script "lib 0 0; press a; wait 3000; wait 1" 2>> "$tlog" || { echo "FAIL $size: banana-ui (titles 2) exited $?"; fail=1; }
    grep -q "launch 0: .*Yellow Planet" "$tlog" || { echo "FAIL $size: the sort title didn't put it first: $(grep launch "$tlog")"; fail=1; }
    rm -f "$T"
    # Power off after sleeping: bananad's banana/resume-boot names the game it saved; at the next start it is launched
    # once (on its own shelf, not Favorites) and the marker is gone; one naming a game no longer there launches nothing
    rlog=$OUT/$size-resume.log
    printf 'rom=%s\nemulator=superdrastic\n' "$FX/roms/nds/Peel Quest.nds" > "$FX/data/resume-boot"
    "$HERE/banana-ui" --headless "$OUT/$size-" --size $size --assets "$HERE/assets/rnds" --systems "$HERE/../system/rootfs/etc/banana/systems.conf" \
        --roms "$FX/roms" --data "$FX/data" --script "home 0; resumeboot; wait 100; resumeboot; wait 100" 2> "$rlog" || { echo "FAIL $size: banana-ui (resume) exited $?"; fail=1; }
    grep -q "launch [0-9]*: .*/nds/Peel Quest.nds (system nds)" "$rlog" || { echo "FAIL $size: the game wasn't resumed at boot: $(grep -i 'launch\|resume' "$rlog")"; fail=1; }
    [ "$(grep -c 'launch [0-9]*:' "$rlog")" = 1 ] || { echo "FAIL $size: resume-boot launched more than once"; fail=1; }
    [ -e "$FX/data/resume-boot" ] && { echo "FAIL $size: resume-boot not consumed"; fail=1; }
    printf 'rom=%s\nemulator=retroarch\n' "$FX/roms/gba/Gone.gba" > "$FX/data/resume-boot"
    "$HERE/banana-ui" --headless "$OUT/$size-" --size $size --assets "$HERE/assets/rnds" --systems "$HERE/../system/rootfs/etc/banana/systems.conf" \
        --roms "$FX/roms" --data "$FX/data" --script "home 0; resumeboot; wait 100" 2>> "$rlog" || { echo "FAIL $size: banana-ui (resume 2) exited $?"; fail=1; }
    grep -q "resume after power-off: no game at .*Gone.gba" "$rlog" || { echo "FAIL $size: a missing game: $(tail -n 3 "$rlog")"; fail=1; }
    rm -f "$FX/data/resume-boot"
    # one on the second SD card, which isn't mounted yet at boot (QA #8): waited for, not given up at once
    printf 'rom=%s\nemulator=retroarch\n' "/run/sd2/roms/gba/Later.gba" > "$FX/data/resume-boot"
    "$HERE/banana-ui" --headless "$OUT/$size-" --size $size --assets "$HERE/assets/rnds" --systems "$HERE/../system/rootfs/etc/banana/systems.conf" \
        --roms "$FX/roms" --data "$FX/data" --script "home 0; resumeboot; wait 100" 2>> "$rlog" || { echo "FAIL $size: banana-ui (resume 3) exited $?"; fail=1; }
    grep -q "resume after power-off: /run/sd2/roms/gba/Later.gba is on the second SD card, waiting" "$rlog" || { echo "FAIL $size: second card game: $(tail -n 3 "$rlog")"; fail=1; }
    [ -e "$FX/data/resume-boot" ] && { echo "FAIL $size: resume-boot (second card) not consumed"; fail=1; }
    rm -f "$FX/data/resume-boot"
    # Screen colours (N3): Display & sound > Screen colours (the sixth row), Red down 10 steps, A: the bottom screen corrected, red 0.90
    # in screen-colors.conf; then X (no correction) and A: "none"
    clog=$OUT/$size-colors.log; CF="$FX/data/screen-colors.conf"; rm -f "$CF"
    L10="press left; press left; press left; press left; press left; press left; press left; press left; press left; press left;"
    "$HERE/banana-ui" --headless "$OUT/$size-" --size $size --assets "$HERE/assets/rnds" --systems "$HERE/../system/rootfs/etc/banana/systems.conf" \
        --roms "$FX/roms" --data "$FX/data" --script "home 0; press start; press a; press down; press down; press down; press down; press down; press a; press down; $L10 shot colors; press a" 2> "$clog" \
        || { echo "FAIL $size: banana-ui (screen colours) exited $?"; fail=1; }
    grep -qx 'screen = bottom' "$CF" && grep -qx 'red = 0.90' "$CF" || { echo "FAIL $size: screen-colors.conf: $(tr '\n' ' ' < "$CF" 2>/dev/null)"; fail=1; }
    # X is no correction on the Screen row too: from the bottom kept, X then Right is None -> the bottom again (it
    # stayed "Correct the bottom" and Right went to the top); Red one step (0.99) and A keep that
    "$HERE/banana-ui" --headless "$OUT/$size-" --size $size --assets "$HERE/assets/rnds" --systems "$HERE/../system/rootfs/etc/banana/systems.conf" \
        --roms "$FX/roms" --data "$FX/data" --script "home 0; press start; press a; press down; press down; press down; press down; press down; press a; press x; press right; press down; press left; press a" 2>> "$clog" \
        || { echo "FAIL $size: banana-ui (screen colours X) exited $?"; fail=1; }
    grep -qx 'screen = bottom' "$CF" && grep -qx 'red = 0.99' "$CF" || { echo "FAIL $size: X didn't set the Screen row to None: $(grep -v '^#' "$CF" | tr '\n' ' ')"; fail=1; }
    "$HERE/banana-ui" --headless "$OUT/$size-" --size $size --assets "$HERE/assets/rnds" --systems "$HERE/../system/rootfs/etc/banana/systems.conf" \
        --roms "$FX/roms" --data "$FX/data" --script "home 0; press start; press a; press down; press down; press down; press down; press down; press a; press x; press a" 2>> "$clog" \
        || { echo "FAIL $size: banana-ui (screen colours 2) exited $?"; fail=1; }
    grep -qx 'screen = none' "$CF" && grep -qx 'red = 1.00' "$CF" || { echo "FAIL $size: X then A didn't take the correction away: $(tr '\n' ' ' < "$CF")"; fail=1; }
    # a file edited by hand (review M1/L1): a capital, an empty value, a letter O for a zero, an indented key: the page
    # opens on them read as meant (empty and wrong: the default, not the bottom of the range), and A keeps that
    printf 'screen = Bottom\nred =\ngreen = O.9\n  blue = 0.8\n' > "$CF"
    "$HERE/banana-ui" --headless "$OUT/$size-" --size $size --assets "$HERE/assets/rnds" --systems "$HERE/../system/rootfs/etc/banana/systems.conf" \
        --roms "$FX/roms" --data "$FX/data" --script "home 0; press start; press a; press down; press down; press down; press down; press down; press a; press a" 2>> "$clog" \
        || { echo "FAIL $size: banana-ui (screen colours 3) exited $?"; fail=1; }
    [ "$(grep -E '^(screen|red|green|blue) ' "$CF" | tr '\n' ' ')" = "screen = bottom red = 1.00 green = 1.00 blue = 0.80 " ] || { echo "FAIL $size: a hand-edited file: $(grep -v '^#' "$CF" | tr '\n' ' ')"; fail=1; }
    rm -f "$CF"
    # a DS game's own Threaded 3D and microphone (#27): X on the first DS game, down to Threaded 3D, right (On), down to
    # Microphone, right (Low), A: both kept per game; then both back to "As in Settings": no lines (and no files) left
    plog3=$OUT/$size-pergame-opts.log; F3="$FX/data/nds-per-game-3d.conf"; FM="$FX/data/nds-per-game-mic.conf"; rm -f "$F3" "$FM"
    "$HERE/banana-ui" --headless "$OUT/$size-" --size $size --assets "$HERE/assets/rnds" --systems "$HERE/../system/rootfs/etc/banana/systems.conf" \
        --roms "$FX/roms" --data "$FX/data" --script "lib 0 0; press x; press down; press down; press down; press right; press down; press right; shot pergame-opts; press a" 2> "$plog3" \
        || { echo "FAIL $size: banana-ui (game options) exited $?"; fail=1; }
    [ "$(cat "$F3" 2>/dev/null)" = "Banana Kart DS.nds = 1" ] && [ "$(cat "$FM" 2>/dev/null)" = "Banana Kart DS.nds = low" ] \
        || { echo "FAIL $size: game options: $(cat "$F3" "$FM" 2>&1 | tr '\n' ' ')"; fail=1; }
    "$HERE/banana-ui" --headless "$OUT/$size-" --size $size --assets "$HERE/assets/rnds" --systems "$HERE/../system/rootfs/etc/banana/systems.conf" \
        --roms "$FX/roms" --data "$FX/data" --script "lib 0 0; press x; press down; press down; press down; press left; press down; press left; press a" 2>> "$plog3" \
        || { echo "FAIL $size: banana-ui (game options 2) exited $?"; fail=1; }
    [ -e "$F3" ] || [ -e "$FM" ] && { echo "FAIL $size: back to As in Settings left $(ls "$F3" "$FM" 2>/dev/null)"; fail=1; }
    # kept at once, B too (B, every page's way out, used to drop the changes without a word): right on Threaded 3D, B
    "$HERE/banana-ui" --headless "$OUT/$size-" --size $size --assets "$HERE/assets/rnds" --systems "$HERE/../system/rootfs/etc/banana/systems.conf" \
        --roms "$FX/roms" --data "$FX/data" --script "lib 0 0; press x; press down; press down; press down; press right; press b" 2>> "$plog3" \
        || { echo "FAIL $size: banana-ui (game options 3) exited $?"; fail=1; }
    [ "$(cat "$F3" 2>/dev/null)" = "Banana Kart DS.nds = 1" ] || { echo "FAIL $size: game options, B: $(cat "$F3" 2>&1)"; fail=1; }
    rm -f "$F3"
    # 4x asked first (it runs hot): left from "As in Settings" on 3D resolution is 4x; B keeps it as it was (nothing
    # written), Use 4x (down, A) writes it; right from 4x (leaving it) asks nothing
    FR="$FX/data/nds-per-game-res.conf"; rm -f "$FR"
    "$HERE/banana-ui" --headless "$OUT/$size-" --size $size --assets "$HERE/assets/rnds" --systems "$HERE/../system/rootfs/etc/banana/systems.conf" \
        --roms "$FX/roms" --data "$FX/data" --script "lib 0 0; press x; press down; press down; press left; shot hot4x; press b" 2>> "$plog3" \
        || { echo "FAIL $size: banana-ui (4x prompt) exited $?"; fail=1; }
    [ -e "$FR" ] && { echo "FAIL $size: 4x kept without Use 4x: $(cat "$FR")"; fail=1; }
    [ "$(convert "$OUT/$size-hot4x-top.png" -format '%[fx:standard_deviation>0.01]' info:)" = 1 ] || { echo "FAIL $size: no 4x prompt drawn"; fail=1; }
    "$HERE/banana-ui" --headless "$OUT/$size-" --size $size --assets "$HERE/assets/rnds" --systems "$HERE/../system/rootfs/etc/banana/systems.conf" \
        --roms "$FX/roms" --data "$FX/data" --script "lib 0 0; press x; press down; press down; press left; press down; press a" 2>> "$plog3" \
        || { echo "FAIL $size: banana-ui (4x prompt 2) exited $?"; fail=1; }
    [ "$(cat "$FR" 2>/dev/null)" = "Banana Kart DS.nds = 4" ] || { echo "FAIL $size: Use 4x: $(cat "$FR" 2>&1)"; fail=1; }
    "$HERE/banana-ui" --headless "$OUT/$size-" --size $size --assets "$HERE/assets/rnds" --systems "$HERE/../system/rootfs/etc/banana/systems.conf" \
        --roms "$FX/roms" --data "$FX/data" --script "lib 0 0; press x; press down; press down; press right" 2>> "$plog3" \
        || { echo "FAIL $size: banana-ui (4x prompt 3) exited $?"; fail=1; }
    [ -e "$FR" ] && { echo "FAIL $size: leaving 4x asked or kept it: $(cat "$FR")"; fail=1; }
    # review fixes on the game page and the keyboard: m7 a pre-filled "Café" loses its whole é to one B (not half of it);
    # m4 a name of its own deleted, then the scraped one typed again: no name of its own; m6 a sharing name with the
    # password step left by B: nothing written
    rlog=$OUT/$size-review.log; TT="$FX/data/game-titles.tsv"
    printf 'nds/Yellow Planet [!].zip\tCafé\t\n' > "$TT"
    run_r() { "$HERE/banana-ui" --headless "$OUT/$size-" --size $size --assets "$HERE/assets/rnds" --systems "$HERE/../system/rootfs/etc/banana/systems.conf" \
        --roms "$FX/roms" --data "$FX/data" --script "$1" 2>> "$rlog" || { echo "FAIL $size: banana-ui (review) exited $?"; fail=1; }; }
    run_r "lib 0 1; press x; press down; press down; press down; press down; press down; press a; press b; press start"
    [ "$(cat "$TT")" = "$(printf 'nds/Yellow Planet [!].zip\tCaf\t')" ] || { echo "FAIL $size: m7 UTF-8 delete: $(od -c "$TT" | head -2 | tr -s ' ')"; fail=1; }
    printf 'nds/Yellow Planet [!].zip\tCustom\t\n' > "$TT"
    run_r "lib 0 1; press x; press down; press down; press down; press down; press down; press a; press b; press b; press b; press b; press b; press b; press start; press a; shot review-m4; press start"
    [ -s "$TT" ] && { echo "FAIL $size: m4 the scraped name typed again kept a name: $(cat "$TT")"; fail=1; }
    rm -f "$TT" "$FX/data/accounts.conf"
    run_r "home 0; press start; press r; press r; press r; press r; press a; press up; press up; press up; press a; press b; press start; press b"
    [ -e "$FX/data/accounts.conf" ] && grep -q smb_user "$FX/data/accounts.conf" && { echo "FAIL $size: m6 the name was written without the password"; fail=1; }
    rm -f "$FX/data/accounts.conf"
    # Box art by name (#28): X on the last DS game, up to Box art (the last row but Delete game), A types (its name, as it is), START
    # looks (a stand-in banana-scrape: two names), down to the second, A fetches it: banana-scrape --match with the
    # game's system, file and that name; B closes the page and the library is read again
    alog2=$OUT/$size-boxart.log; SL=$OUT/$size-fake-scrape.txt; rm -f "$SL"
    BANANA_SCRAPE="$HERE/tests/fake-scrape.sh" FAKE_SCRAPE_LOG="$SL" "$HERE/banana-ui" --headless "$OUT/$size-" --size $size --assets "$HERE/assets/rnds" \
        --systems "$HERE/../system/rootfs/etc/banana/systems.conf" --roms "$FX/roms" --data "$FX/data" \
        --script "lib 0 4; press x; press up; press up; shot boxart-row; press a; press start; shot boxart-list; press down; press a; shot boxart-done; press b" 2> "$alog2" \
        || { echo "FAIL $size: banana-ui (box art) exited $?"; fail=1; }
    [ "$(cat "$SL" 2>/dev/null)" = "nds|$FX/roms/nds/Yellow Planet [!].zip|Yellow Planet - Second Orbit (Europe)" ] || { echo "FAIL $size: --match: $(cat "$SL" 2>/dev/null)"; fail=1; }
    grep -q 'titles nds/Yellow Planet' "$alog2" || { echo "FAIL $size: closing the page didn't read the library again"; fail=1; }
    [ -e "$FX/data/game-titles.tsv" ] && { echo "FAIL $size: a cover alone wrote game-titles.tsv"; fail=1; }
    rm -f "$FX/data/game-titles.tsv"
    # the game page (X on a DS game): its own shader, Down to the Shader row, Right to None, A keeps it in
    # nds-per-game-shader.conf; then Left back to "As in Settings" and the file goes (nothing chosen: no file)
    plog=$OUT/$size-pergame.log; G="$FX/data/nds-per-game-shader.conf"; rm -f "$G"
    run_pg() { "$HERE/banana-ui" --headless "$OUT/$size-" --size $size --assets "$HERE/assets/rnds" \
        --systems "$HERE/../system/rootfs/etc/banana/systems.conf" --roms "$FX/roms" --data "$FX/data" --script "$1" 2>> "$plog" ||
        { echo "FAIL $size: banana-ui (game page) exited $?"; fail=1; }; }
    run_pg "lib 0 0; press x; wait 300; shot pergame; press down; press right; wait 300; shot pergame-shader; press a; wait 300; wait 1"
    grep -q ' = none$' "$G" 2>/dev/null || { echo "FAIL $size: the game's shader wasn't kept: $(cat "$G" 2>&1)"; fail=1; }
    run_pg "lib 0 0; press x; wait 300; press down; shot pergame-none; press left; press a; wait 300; wait 1"
    [ -e "$G" ] && { echo "FAIL $size: back to As in Settings left $G"; fail=1; }
    for s in pergame pergame-shader pergame-none; do
        [ -s "$OUT/$size-$s-top.png" ] && [ -s "$OUT/$size-$s-bot.png" ] || { echo "FAIL $size: no $s shot"; fail=1; }
    done
    # Settings > System's two resets: the system one puts the menu's own keys back (theme, sounds, ...) and leaves
    # the emulator's (core.gba, ra_shader) alone; then the emulator one puts those back and leaves the menu's alone
    rlog=$OUT/$size-reset.log
    # (ra_vsync = lcd: a value from another key's list, which the start (normalizeSettings) must put back; the system
    # reset leaves ra_vsync alone)
    for kv in "theme = light" "sounds = off" "core.gba = mgba" "ra_shader = crt" "ra_vsync = lcd"; do
        k=${kv%% *}
        grep -q "^$k = " "$FX/data/settings.conf" && sed -i "s/^$k = .*/$kv/" "$FX/data/settings.conf" || printf '%s\n' "$kv" >> "$FX/data/settings.conf"
    done
    # groups: Display & sound, Sleep & power, Nintendo DS, Other systems, Network, System (five R's); System's rows
    # from the first (Language, Time zone, Update, Test versions, Update check, Box art, Box art from, Previous version,
    # First-time setup): down nine to Reset system settings, A twice
    run_reset() { "$HERE/banana-ui" --headless "$OUT/$size-" --size $size --assets "$HERE/assets/rnds" \
        --systems "$HERE/../system/rootfs/etc/banana/systems.conf" --roms "$FX/roms" --data "$FX/data" --script "$1" 2>> "$rlog" ||
        { echo "FAIL $size: banana-ui (reset) exited $?"; fail=1; }; }
    run_reset "home 0; press start; press r; press r; press r; press r; press r; press a; press down; press down; press down; press down; press down; press down; press down; press down; press down; press a; press a; wait 300; shot reset-system"
    grep -q '^theme = dark' "$FX/data/settings.conf" || { echo "FAIL $size: system reset didn't restore theme: $(grep '^theme' "$FX/data/settings.conf")"; fail=1; }
    grep -q '^sounds = on' "$FX/data/settings.conf" || { echo "FAIL $size: system reset didn't restore sounds"; fail=1; }
    grep -q '^ra_vsync = on' "$FX/data/settings.conf" || { echo "FAIL $size: an invalid ra_vsync was kept: $(grep '^ra_vsync' "$FX/data/settings.conf")"; fail=1; }
    grep -q '^core.gba = mgba' "$FX/data/settings.conf" || { echo "FAIL $size: system reset wiped an emulator core: $(grep '^core.gba' "$FX/data/settings.conf")"; fail=1; }
    grep -q '^ra_shader = crt' "$FX/data/settings.conf" || { echo "FAIL $size: system reset wiped ra_shader"; fail=1; }
    # down ten from the first row to Reset emulator settings, A twice (theme and sounds are already back)
    run_reset "home 0; press start; press r; press r; press r; press r; press r; press a; press down; press down; press down; press down; press down; press down; press down; press down; press down; press down; press a; press a; wait 300; shot reset-emulator"
    grep -q '^core.gba = gpsp' "$FX/data/settings.conf" || { echo "FAIL $size: emulator reset didn't restore core.gba: $(grep '^core.gba' "$FX/data/settings.conf")"; fail=1; }
    grep -q '^ra_shader = off' "$FX/data/settings.conf" || { echo "FAIL $size: emulator reset didn't restore ra_shader"; fail=1; }
    grep -q '^theme = dark' "$FX/data/settings.conf" || { echo "FAIL $size: emulator reset wiped a system key"; fail=1; }
    for s in reset-system reset-emulator; do
        [ -s "$OUT/$size-$s-top.png" ] && [ -s "$OUT/$size-$s-bot.png" ] || { echo "FAIL $size: no $s shot"; fail=1; }
    done
    # a layout theme (the image's canvas.json): every screen drawn from its JSON, home -> library -> a launch, a
    # tap on a system, and no complaint about the theme in the log
    llog=$OUT/$size-layout.log
    "$HERE/banana-ui" --headless "$OUT/$size-layout-" --size $size --assets "$HERE/assets/rnds" --banana "$HERE/assets/banana" \
        --systems "$HERE/../system/rootfs/etc/banana/systems.conf" --roms "$FX/roms" --data "$FX/data" \
        --theme "$HERE/../system/rootfs/usr/share/banana/themes/canvas.json" \
        --script "home 0; wait 1000; shot home; press right; wait 1000; shot home2; touch 1 160 250; wait 1000; press a; wait 1000; shot lib; \
            press down; wait 600; shot lib2; press a; wait 350; shot launch; wait 1000; wait 1" 2> "$llog" || { echo "FAIL $size: banana-ui (layout) exited $?"; fail=1; }
    grep -q "theme:" "$llog" && { echo "FAIL $size: the layout theme: $(grep 'theme:' "$llog" | head -3)"; fail=1; }
    grep -q "touch 160 250: used" "$llog" || { echo "FAIL $size: the layout theme's tap on a system"; fail=1; }
    grep -q "press down: used, view 1 sys 0 game 1" "$llog" || { echo "FAIL $size: the layout theme's own buttons (down: the next game)"; fail=1; }
    grep -q "launch 1: .*Monkey Puzzle League.nds" "$llog" || { echo "FAIL $size: the layout theme's launch: $(grep launch "$llog")"; fail=1; }
    for s in home home2 lib lib2 launch; do
        for p in top bot; do
            f=$OUT/$size-layout-$s-$p.png
            [ -s "$f" ] || { echo "FAIL $size: no $f"; fail=1; continue; }
            [ "$(convert "$f" -format '%[fx:standard_deviation>0.01]' info:)" = 1 ] || { echo "FAIL $size: $f is blank"; fail=1; }
        done
    done
    # the game panel with the Pokémon companion (a made-up Emerald with made-up icons, RetroArch's replies from the
    # decoder's test): the party, a member's summary by touch, another member from the strip, back, a battle (shown by
    # itself), the Game tab (the panel's tiles and achievements); then the same in the light theme and in Chinese
    klog=$OUT/$size-pokemon.log
    pkscript="home 0; gamepanel 1 $OUT/poke.gba; gshot pk-wait; pokefeed $OUT/poke-party.txt; gshot pk-party; gtouch 100 70; gshot pk-mon; \
        gtouch 134 384; gshot pk-mon2; gtouch 570 384; gshot pk-back; wait 300; pokefeed $OUT/poke-battle.txt; wait 300; \
        pokefeed $OUT/poke-battle.txt; gshot pk-battle; gtouch 580 447; gshot pk-game"
    "$HERE/banana-ui" --headless "$OUT/$size-" --size $size --assets "$HERE/assets/rnds" --systems "$HERE/../system/rootfs/etc/banana/systems.conf" \
        --roms "$FX/roms" --data "$FX/data" --script "$pkscript; gamepanel 1 $FX/roms/nds/Peel Quest.nds; gshot pk-none" 2> "$klog" ||
        { echo "FAIL $size: banana-ui (Pokémon companion) exited $?"; fail=1; }
    kcheck() { grep -q "$1" "$klog" || { echo "FAIL $size: no '$1' (Pokémon companion)"; fail=1; }; }
    kcheck "gamepanel .*poke.gba: companion Pokémon Emerald"
    kcheck "413 icons (413 palette numbers, 3 palettes), 6 growth rates"
    kcheck "pokefeed .*poke-party.txt: asked 3, party 4, battle 0"
    kcheck "pokefeed .*poke-battle.txt: asked 6, party 4, battle 1, foes 1"
    kcheck "gamepanel .*Peel Quest.nds: companion none"
    LX=$OUT/fixture-light; rm -rf "$LX"; cp -r "$FX/data" "$LX"; printf 'theme = light\nlanguage = zh-cn\n' >> "$LX/settings.conf"
    "$HERE/banana-ui" --headless "$OUT/$size-zh-" --size $size --assets "$HERE/assets/rnds" --systems "$HERE/../system/rootfs/etc/banana/systems.conf" \
        --roms "$FX/roms" --data "$LX" --script "$pkscript" 2> "$OUT/$size-pokemon-zh.log" || { echo "FAIL $size: banana-ui (Pokémon companion, light, zh-cn) exited $?"; fail=1; }
    for s in pk-wait pk-party pk-mon pk-mon2 pk-back pk-battle pk-game pk-none zh-pk-party zh-pk-mon zh-pk-battle zh-pk-game; do
        f=$OUT/$size-$s-bot.png
        [ -s "$f" ] || { echo "FAIL $size: no $f"; fail=1; continue; }
        [ "$(convert "$f" -format '%[fx:standard_deviation>0.01]' info: 2>/dev/null)" = 1 ] || { echo "FAIL $size: $f is blank"; fail=1; }
    done
    # the game panel with the game companion (gamecomp.h; definitions from the companion data in tests/companions):
    # Castlevania's seven pages by their tabs with RetroArch's replies for each, a list row opened and closed, the Game
    # tab; Golden Sun's party list; Tetris Worlds (reference pages: nothing read); Emerald (the Pokémon companion's)
    glog=$OUT/$size-gamecomp.log
    tab() { echo "gtouch $((18 + 604 * $1 / $2 + 302 / $2)) 447"; }
    gcs="home 0; gcpanel 1 $HERE/tests/companions $OUT/gc-castlevania.gba; gshot gc-wait; gcfeed $OUT/gc-cv-status.txt; gshot gc-status"
    i=1; for pg in detail battle map bag collection progress; do
        gcs="$gcs; $(tab $i 8); gcfeed $OUT/gc-cv-$pg.txt; gshot gc-$pg"; i=$((i + 1))
    done
    gcs="$gcs; $(tab 4 8); gcfeed $OUT/gc-cv-bag.txt; gshot gc-bag2; gtouch 200 175; gshot gc-row; gtouch 200 175; gshot gc-row-back; $(tab 7 8); gshot gc-game"
    gcs="$gcs; gcpanel 1 $HERE/tests/companions $OUT/gc-goldensun.gba; gcfeed $OUT/gc-gs-status.txt; gshot gc-gs-status; $(tab 1 8); gcfeed $OUT/gc-gs-detail.txt; gshot gc-gs-detail"
    gcs="$gcs; gcpanel 1 $HERE/tests/companions $OUT/gc-tetris.gba; gcfeed -; gshot gc-tetris; gcpanel 1 $HERE/tests/companions $OUT/gc-emerald.gba"
    "$HERE/banana-ui" --headless "$OUT/$size-" --size $size --assets "$HERE/assets/rnds" --systems "$HERE/../system/rootfs/etc/banana/systems.conf" \
        --roms "$FX/roms" --data "$FX/data" --script "$gcs" 2> "$glog" || { echo "FAIL $size: banana-ui (game companion) exited $?"; fail=1; }
    gcheck() { grep -q "$1" "$glog" || { echo "FAIL $size: no '$1' (game companion)"; fail=1; }; }
    gcheck "gcpanel .*gc-castlevania.gba: companion Castlevania"
    gcheck "gcfeed .*gc-cv-status.txt: asked [1-6] .* seen 1"
    gcheck "gcpanel .*gc-goldensun.gba: companion Golden Sun"
    gcheck "gcfeed -: asked 0 () seen 1"
    gcheck "gcpanel .*gc-emerald.gba: companion none"
    for s in gc-wait gc-status gc-detail gc-battle gc-map gc-bag gc-collection gc-progress gc-row gc-row-back gc-game gc-gs-status gc-gs-detail gc-tetris; do
        f=$OUT/$size-$s-bot.png
        [ -s "$f" ] || { echo "FAIL $size: no $f"; fail=1; continue; }
        [ "$(convert "$f" -format '%[fx:standard_deviation>0.01]' info: 2>/dev/null)" = 1 ] || { echo "FAIL $size: $f is blank"; fail=1; }
    done
    for pair in "gc-wait gc-status" "gc-status gc-detail" "gc-detail gc-map" "gc-map gc-bag" "gc-bag gc-row" "gc-row gc-row-back" "gc-collection gc-progress" "gc-progress gc-game" "gc-gs-status gc-gs-detail"; do
        a=${pair% *}; b=${pair#* }
        cmp -s "$OUT/$size-$a-bot.png" "$OUT/$size-$b-bot.png" && { echo "FAIL $size: $a and $b are the same picture (game companion)"; fail=1; }
    done
    # the menu's UX checks (the row's ends, the box art's frame, text on the accent, ...)
    . "$HERE/tests/ux.sh"
    # the first-time setup's slides and the first boot's arrival
    . "$HERE/tests/setup.sh"
    # each view drew something of its own (not the same frame again)
    for pair in "pk-party pk-mon" "pk-mon pk-mon2" "pk-mon2 pk-back" "pk-back pk-battle" "pk-battle pk-game" "pk-party zh-pk-party"; do
        a=${pair% *}; b=${pair#* }
        cmp -s "$OUT/$size-$a-bot.png" "$OUT/$size-$b-bot.png" && { echo "FAIL $size: $a and $b are the same picture"; fail=1; }
    done
    # Bshare's pages (settings_bshare.cpp) against a stand-in helper (its arguments noted, a send's state written): a
    # device's request accepted into a folder chosen on the card (Location), a game in roms/gb (the first folder) sent
    # to a device seen, Options' Receive when closed turned to Ask
    blog=$OUT/$size-bshare.log; BRUN=$OUT/bsrun
    rm -rf "$BRUN"; mkdir -p "$BRUN"; : > "$blog"; sed -i '/^bshare_/d' "$FX/data/settings.conf"
    printf '#!/bin/sh\necho "$@" >> "%s/args"\n[ "$1" = send ] && printf "sending\\tMy Mac\\t1\\t2\\t500\\t1000\\n" > "%s/bshare-send"\nexit 0\n' "$BRUN" "$BRUN" > "$BRUN/helper"
    chmod +x "$BRUN/helper"
    printf 'id\tabc123\nfrom\tPixel 8\nmodel\tPixel\nfiles\t3\nbytes\t12400000\nfirst\tPokemon Emerald.gba\n' > "$BRUN/bshare-request"
    printf 'mac-fp\t10.0.0.5\t53317\thttps\tMy Mac\tMacBook\tdesktop\t1\nph-fp\t10.0.0.6\t53317\thttps\tPixel 8\tPixel\tmobile\t1\n' > "$BRUN/bshare-peers"
    run_bs() { BANANA_BSHARE="$BRUN/helper" BSHARE_RUN="$BRUN" "$HERE/banana-ui" --headless "$OUT/$size-" --size $size --assets "$HERE/assets/rnds" \
        --systems "$HERE/../system/rootfs/etc/banana/systems.conf" --roms "$FX/roms" --data "$FX/data" --script "$1" 2>> "$blog" ||
        { echo "FAIL $size: banana-ui (bshare) exited $?"; fail=1; }; }
    # a request there as the app opens: its Receive page at Accept; Location: the card itself, chosen; then Accept
    run_bs "homeid bshare; shot bs-tile; press a; wait 1; shot bs-request; press down; press down; press a; shot bs-location; press a; shot bs-card; \
        press a; shot bs-located; press up; press up; press a; wait 1; shot bs-accepted"
    grep -q "press a: banana:bshare open" "$blog" || { echo "FAIL $size: bshare: the tile didn't open its page"; fail=1; }
    grep -q "^accept abc123 $FX$" "$BRUN/bshare-answer" 2>/dev/null || { echo "FAIL $size: bshare: no accept into the card ($(cat "$BRUN/bshare-answer" 2>/dev/null))"; fail=1; }
    rm -f "$BRUN/bshare-request"
    # Send: Device (My Mac), Files (roms/gb's game, A picks it), START done, START sends; then Options' Receive when
    # closed to Ask
    run_bs "homeid bshare; press a; wait 1; shot bs-home; press down; press a; shot bs-send; press a; shot bs-devices; press a; \
        press down; press a; press a; press down; press a; press a; press a; shot bs-files; press start; shot bs-send-ready; press start; wait 1; \
        shot bs-status; press b; press b; press down; press a; press right; shot bs-options; press down; press a; shot bs-options-pin"
    for i in $(seq 30); do grep -q "^send " "$BRUN/args" && break; sleep 0.1; done    # (the helper started on its own)
    grep -q "^send mac-fp $FX/roms/gb/Pocket Peel.gb$" "$BRUN/args" || { echo "FAIL $size: bshare: no send to My Mac: $(cat "$BRUN/args")"; fail=1; }
    grep -q '^bshare_receive = ask$' "$FX/data/settings.conf" || { echo "FAIL $size: bshare: Options' Receive when closed didn't turn to Ask"; fail=1; }
    grep -q '^bshare_require_pin = on$' "$FX/data/settings.conf" && grep -Eq '^bshare_pin = [0-9]{4}$' "$FX/data/settings.conf" ||
        { echo "FAIL $size: bshare: Options' Require PIN didn't turn on with a PIN: $(grep '^bshare_' "$FX/data/settings.conf")"; fail=1; }
    grep -q "^serve$" "$BRUN/args" || { echo "FAIL $size: bshare: the helper wasn't started to serve"; fail=1; }
    sed -i '/^bshare_/d' "$FX/data/settings.conf"
    # a device that asks for a PIN (LocalSend's Require PIN): the helper says "pin", the keyboard comes up on its own,
    # the PIN typed and START: the same file sent again with it
    : > "$BRUN/args"; rm -f "$BRUN/bshare-send"
    printf '#!/bin/sh\necho "$@" >> "%s/args"\nif [ "$1" = send ] && [ "$2" = --pin ]; then printf "done\\tMy Mac\\t1\\t1\\t1000\\t1000\\n" > "%s/bshare-send"; elif [ "$1" = send ]; then printf "pin\\tMy Mac\\t0\\t1\\t0\\t1000\\n" > "%s/bshare-send"; fi\nexit 0\n' "$BRUN" "$BRUN" "$BRUN" > "$BRUN/helper"
    run_bs "homeid bshare; press a; wait 1; shot bs-pin-home; press down; press a; shot bs-pin-send; press a; shot bs-pin-devices; press a; \
        press down; press a; press a; press down; press a; press a; press a; shot bs-pin-files; press start; shot bs-pin-ready; press start; wait 1; \
        shot bs-pin-status; sleep 800; shot bs-pin; type 4242; press start; sleep 800; shot bs-pin-sent"
    for i in $(seq 30); do grep -q "^send --pin" "$BRUN/args" && break; sleep 0.1; done
    grep -q "^send mac-fp $FX/roms/gb/Pocket Peel.gb$" "$BRUN/args" && grep -q "^send --pin 4242 mac-fp $FX/roms/gb/Pocket Peel.gb$" "$BRUN/args" ||
        { echo "FAIL $size: bshare: the PIN wasn't asked for and sent: $(cat "$BRUN/args")"; fail=1; }
    sed -i '/^bshare_/d' "$FX/data/settings.conf"
    grep 'bench:' "$log" | sed "s/^banana-ui: /$size /"
done
# updates over the air (#69): the update script against a stand-in for GitHub, and the Update page
OUT="$OUT" "$HERE/tests/ota.sh" || fail=1
# home server sync (banana-sync) against a stand-in RomM and, when its source is here, the real Romhost
OUT="$OUT" "$HERE/tests/sync.sh" || fail=1
[ $fail = 0 ] && echo "banana-ui: all checks passed ($OUT)"
exit $fail

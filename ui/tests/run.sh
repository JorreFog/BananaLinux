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
    # password), past the Box art step (not started: no network here), Finish writes setup-done; the accounts file is what the launchers read (no login with the server
# here: BANANA_RA_CHECK=0)
    slog=$OUT/$size-setup.log
    rm -f "$FX/data/setup-done" "$FX/data/accounts.conf"
    BANANA_RA_CHECK=0 BANANA_WIFI="$HERE/tests/fake-wifi.sh" FAKE_WIFI_CONF="$FX/data/wifi.conf" "$HERE/banana-ui" --headless "$OUT/$size-" --size $size \
        --assets "$HERE/assets/rnds" --systems "$HERE/../system/rootfs/etc/banana/systems.conf" --roms "$FX/roms" --data "$FX/data" \
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
        --systems "$HERE/../system/rootfs/etc/banana/systems.conf" --roms "$FX/roms" --data "$FX/data" \
        --script "lib 0 0; press a; wait 3000; wait 1" 2> "$flog" || { echo "FAIL $size: banana-ui (favorites) exited $?"; fail=1; }
    grep -q "launch 0: .*Orchard Tactics.gba (system gba)" "$flog" || { echo "FAIL $size: the favourite: $(grep launch "$flog")"; fail=1; }
    mv "$OUT/favorites.txt.keep" "$FX/data/favorites.txt"; rm -f "$FX/data/favorite-systems.txt"
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
    for kv in "theme = light" "sounds = off" "core.gba = mgba" "ra_shader = crt"; do
        k=${kv%% *}
        grep -q "^$k = " "$FX/data/settings.conf" && sed -i "s/^$k = .*/$kv/" "$FX/data/settings.conf" || printf '%s\n' "$kv" >> "$FX/data/settings.conf"
    done
    # groups: Display & sound, Sleep & power, Nintendo DS, Other systems, Network, System (five R's); System's rows
    # from Time zone: down seven to Reset system settings, A twice
    run_reset() { "$HERE/banana-ui" --headless "$OUT/$size-" --size $size --assets "$HERE/assets/rnds" \
        --systems "$HERE/../system/rootfs/etc/banana/systems.conf" --roms "$FX/roms" --data "$FX/data" --script "$1" 2>> "$rlog" ||
        { echo "FAIL $size: banana-ui (reset) exited $?"; fail=1; }; }
    run_reset "home 0; press start; press r; press r; press r; press r; press r; press a; press down; press down; press down; press down; press down; press down; press down; press a; press a; wait 300; shot reset-system"
    grep -q '^theme = dark' "$FX/data/settings.conf" || { echo "FAIL $size: system reset didn't restore theme: $(grep '^theme' "$FX/data/settings.conf")"; fail=1; }
    grep -q '^sounds = on' "$FX/data/settings.conf" || { echo "FAIL $size: system reset didn't restore sounds"; fail=1; }
    grep -q '^core.gba = mgba' "$FX/data/settings.conf" || { echo "FAIL $size: system reset wiped an emulator core: $(grep '^core.gba' "$FX/data/settings.conf")"; fail=1; }
    grep -q '^ra_shader = crt' "$FX/data/settings.conf" || { echo "FAIL $size: system reset wiped ra_shader"; fail=1; }
    # down eight from Time zone to Reset emulator settings, A twice (theme and sounds are already back)
    run_reset "home 0; press start; press r; press r; press r; press r; press r; press a; press down; press down; press down; press down; press down; press down; press down; press down; press a; press a; wait 300; shot reset-emulator"
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
    # Downloads (the shelf's tile), with banana-fetch itself (built when this computer has its libraries; the top
    # Makefile's test does it) and its test protocol, "dir", standing in for a server (a folder here; only with
    # BANANA_FETCH_TEST=1). Headless, the menu runs banana-fetch's runner to the end at once. A server's folder
    # browsed, two files marked and downloaded into the DS folder (the zip unpacked, as asked), the connection lost with
    # Wi-Fi off (the folder back once it connects, B out of it meanwhile), a locked server's password typed, an archive
    # on the card unpacked where it is (and deleted, as asked), an option changed, a server added with the keyboard; a
    # job's runner at work beside the menu (its progress, the retry's countdown, X Stop and Y Cancel tapped), and the
    # chips tapped on the other pages
    if [ -x "$HERE/banana-fetch" ]; then
        dlog=$OUT/$size-downloads.log; NAS=$OUT/nas; RUN=$OUT/run
        dlclean() {
            rm -rf "$NAS" "$RUN" "$FX/data/downloads" "$FX/data/nas.conf" "$FX/data/nas.conf.lock" "$FX/data/nas.key" "$FX/data/logs/downloads.log" \
                "$FX/roms/nds/Alpha Quest.nds" "$FX/roms/nds/Beta Racer.nds" "$FX/roms/nds/Beta Racer.zip" "$FX/Delta.zip" "$FX/Delta.gba" "$FX/.banana-unpack" \
                "$FX/roms/nds/Gamma Big.nds" "$FX/roms/nds/Gamma Big.nds.part" "$FX/roms/gba/Echo.gba" "$FX/roms/gba/Echo Pack.zip"
            sed -i '/^downloads_/d' "$FX/data/settings.conf"
        }
        dlclean
        mkdir -p "$NAS/Games/nds" "$NAS/Games/gba" "$RUN"
        head -c 70000 /dev/urandom > "$NAS/Games/nds/Alpha Quest.nds"
        head -c 1000 /dev/zero > "$NAS/Games/gba/Echo.gba"
        python3 - "$NAS/Games/nds/Beta Racer.zip" "$FX/Delta.zip" "$NAS/Games/gba/Echo Pack.zip" <<'PY'
import sys, zipfile
with zipfile.ZipFile(sys.argv[1], 'w', zipfile.ZIP_DEFLATED) as z:
    z.writestr('Beta Racer.nds', b'B' * 50000)
with zipfile.ZipFile(sys.argv[2], 'w', zipfile.ZIP_DEFLATED) as z:
    z.writestr('Delta/Delta.gba', b'D' * 30000)
with zipfile.ZipFile(sys.argv[3], 'w', zipfile.ZIP_DEFLATED) as z:
    z.writestr('Echo Pack.gba', b'E' * 20000)
PY
        printf '[locked]\nname = Locked\nprotocol = dir\nhost = %s\nuser = u\nremember = no\n\n[testnas]\nname = Test NAS\nprotocol = dir\nhost = %s\nguest = yes\nstart = /Games\n' \
            "$NAS" "$NAS" > "$FX/data/nas.conf"
        : > "$dlog"
        run_dl() { BANANA_FETCH="$HERE/banana-fetch" BANANA_FETCH_TEST=1 BANANA_FETCH_TEST_FAST=1 BANANA_FETCH_TEST_PASSWORD=qwe BANANA_RUN="$RUN" \
            "$HERE/banana-ui" --headless "$OUT/$size-" --size $size --assets "$HERE/assets/rnds" \
            --systems "$HERE/../system/rootfs/etc/banana/systems.conf" --roms "$FX/roms" --data "$FX/data" --script "$1" 2>> "$dlog" ||
            { echo "FAIL $size: banana-ui (downloads) exited $?"; fail=1; }; }
        dlcheck() { grep -q "$1" "$dlog" || { echo "FAIL $size: downloads: no '$1'"; fail=1; }; }
        # (headless, a press the menu takes isn't followed by its update: "wait 1" lets banana-fetch's answer in)
        # home: Locked, Test NAS, Add a server, Archives on the card, Options. Test NAS opens on /Games (gba, nds); in
        # nds Alpha Quest.nds and Beta Racer.zip, both marked, X; the Download page: Card, Folder (Nintendo DS, from the
        # .nds), Unpack archives (No: the DS plays zips) turned to Yes, which brings Delete archives after (No); Download.
        # B on the job's page comes back to nds, its marks gone; B up to /Games, to / (listed only here) and home
        run_dl "fetchdone; homeid downloads; shot dl-tile; press a; shot dl-home; press down; press a; wait 1; shot dl-browse; press down; press a; wait 1; \
            press y; press down; press y; shot dl-marked; press x; shot dl-target; press down; press down; press right; shot dl-unpack; \
            press down; press down; press a; wait 300; shot dl-status; fetchdone; shot dl-done; press b; shot dl-back; \
            press b; wait 1; press b; wait 1; press b; shot dl-home-done"
        dlcheck "press a: banana:downloads open"
        dlcheck "downloads: connected to Test NAS (DIR), home /"
        dlcheck "downloads: /Games/nds: 2 entries"
        dlcheck "2 files from Test NAS into $FX/roms/nds"
        dlcheck "downloads: a job finished"
        dlcheck "downloads: /: 1 entries"
        cmp -s "$NAS/Games/nds/Alpha Quest.nds" "$FX/roms/nds/Alpha Quest.nds" || { echo "FAIL $size: Alpha Quest.nds wasn't downloaded"; fail=1; }
        [ -f "$FX/roms/nds/Beta Racer.nds" ] && cmp -s "$NAS/Games/nds/Beta Racer.zip" "$FX/roms/nds/Beta Racer.zip" ||
            { echo "FAIL $size: Beta Racer.zip wasn't downloaded and unpacked, the zip kept"; fail=1; }
        ls "$FX/data/downloads/"*.finished > /dev/null 2>&1 || { echo "FAIL $size: no finished job in banana/downloads"; fail=1; }
        grep -q '^target /Games/nds = data:roms/nds$' "$FX/data/nas.conf" || { echo "FAIL $size: the folder chosen wasn't remembered"; fail=1; }
        # the lid shut in a server's folder: Wi-Fi unloaded, back only a while after the lid opens. In nds the connection
        # is gone (the 3rd listing) and connecting again fails (the 2nd connection): SELECT reads the folder in vain,
        # then B connects again by itself and goes up to /Games. And with B first: home, not nds for good
        export BANANA_FETCH_TEST_DROPLIST=3 BANANA_FETCH_TEST_OFFLINE=2
        n=$(wc -l < "$dlog")
        run_dl "homeid downloads; press a; press down; press down; press a; wait 1; press down; press a; wait 1; \
            press select; wait 1; press b; wait 1; shot dl-reconnected"
        tail -n +$((n + 1)) "$dlog" | grep -A 4 "downloads: list: network: " | grep -q "downloads: /Games: 2 entries" ||
            { echo "FAIL $size: the server's folder didn't come back after connecting again failed"; fail=1; }
        n=$(wc -l < "$dlog")
        run_dl "homeid downloads; press a; press down; press down; press a; wait 1; press down; press a; wait 1; \
            press b; wait 1; shot dl-unreachable; press b"
        tail -n +$((n + 1)) "$dlog" | grep -A 2 "downloads: list: network: " | grep -q "press b: settings closed" ||
            { echo "FAIL $size: B didn't get out of the folder of a server that can't be reached"; fail=1; }
        unset BANANA_FETCH_TEST_DROPLIST BANANA_FETCH_TEST_OFFLINE
        # home: Last download, Locked (its password typed on the keyboard: qwe), ... Archives on the card: this card's
        # data, roms and Delta.zip: Then delete it (Options' Ask) to Yes, Unpack here (its one folder unwrapped; B then
        # comes back to the card's folder, without Delta.zip, and B twice more home, on the job's row, the first);
        # Options (the last row): Unpack after a download to Always; Add a server: SFTP, its address typed (qwe), START
        # saves it
        run_dl "homeid downloads; press a; press down; press a; shot dl-password; $(key 1,0 1,1 1,2) press start; wait 1; shot dl-locked; press b; \
            press down; press down; press down; press a; press a; press down; press down; press a; shot dl-local; press down; press down; press right; \
            shot dl-local-delete; press up; press up; press a; wait 300; shot dl-unpacked; press b; shot dl-local-back; press b; press b; \
            press up; press a; press right; shot dl-options; press b; press up; press up; press a; press down; press right; \
            press down; press a; $(key 1,0 1,1 1,2) press start; shot dl-edit; press start; wait 1; shot dl-saved"
        dlcheck "downloads: connected to Locked (DIR), home /"
        dlcheck "unpacked into $FX$"
        dlcheck "downloads: server .* saved"
        [ "$(cat "$FX/Delta.gba" 2>/dev/null | wc -c)" = 30000 ] || { echo "FAIL $size: Delta.zip wasn't unpacked on the card"; fail=1; }
        [ -e "$FX/Delta.zip" ] && { echo "FAIL $size: Unpack here didn't delete Delta.zip, as Then delete it said"; fail=1; }
        grep -q '^downloads_unpack = always$' "$FX/data/settings.conf" || { echo "FAIL $size: Options didn't keep Unpack after a download"; fail=1; }
        grep -q '^host = qwe$' "$FX/data/nas.conf" && grep -q '^protocol = sftp$' "$FX/data/nas.conf" ||
            { echo "FAIL $size: the server added isn't in nas.conf: $(grep -c '^\[' "$FX/data/nas.conf") sections"; fail=1; }
        # a job's runner at work beside the menu (started here, a piece every 100 ms, the connection cut at 1 MB): the
        # status page while it downloads (its two bars) and while it waits to try again; X Stop tapped twice stops it,
        # its part and its job kept; then Y Cancel tapped twice deletes both. ("sleep": real time, for the runner)
        head -c 3000000 /dev/urandom > "$NAS/Games/nds/Gamma Big.nds"
        JOB=$FX/data/downloads/20990101-000000-1.job
        printf 'title = Gamma Big.nds\nserver = testnas\nunpack = no\ntemp_root = %s\nget\t3000000\t/Games/nds/Gamma Big.nds\t%s\n' "$FX" "$FX/roms/nds/Gamma Big.nds" > "$JOB"
        BANANA_DATA="$FX/data" BANANA_RUN="$RUN" BANANA_FETCH_TEST=1 BANANA_FETCH_TEST_DELAY=100 BANANA_FETCH_TEST_CUT=1000000 "$HERE/banana-fetch" run 2>> "$dlog" & rp=$!
        upto() { i=0; until grep -q "^$1 " "$RUN/fetch/status" 2>/dev/null || [ $i -ge 100 ]; do sleep 0.1; i=$((i + 1)); done; }
        upto get
        run_dl "homeid downloads; press a; press a; shot dl-progress"
        upto retry
        run_dl "homeid downloads; press a; press a; shot dl-retry; touch 1 180 442; shot dl-stop; touch 1 180 442; sleep 1500; wait 600; shot dl-stopped"
        wait $rp || true
        grep -q '^stopped' "$RUN/fetch/status" && [ -f "$JOB" ] && [ -s "$FX/roms/nds/Gamma Big.nds.part" ] ||
            { echo "FAIL $size: X Stop (tapped) didn't stop the download, its part and job kept: $(cat "$RUN/fetch/status")"; fail=1; }
        run_dl "homeid downloads; press a; press a; touch 1 180 442; shot dl-cancel; touch 1 180 442; wait 1; shot dl-cancelled"
        dlcheck "$JOB cancelled"
        [ ! -e "$JOB" ] && [ ! -e "$FX/roms/nds/Gamma Big.nds.part" ] || { echo "FAIL $size: Y Cancel (tapped) left the job or its part"; fail=1; }
        cmp -s "$OUT/$size-dl-progress-top.png" "$OUT/$size-dl-retry-top.png" && { echo "FAIL $size: the retry's countdown looks like the progress"; fail=1; }
        # the chips tapped where they are drawn: home (Last download, Locked, qwe, Test NAS...): Y Edit on Locked, START
        # saves it; Test NAS's /Games: X Get on gba (Options' Ask again): its Download page has a Zips row (the GBA
        # plays them zipped: kept as they are, its job says so; B back in /Games, then up to / and home); Add a server:
        # A Type on Address (ewq), START Save
        sed -i '/^downloads_/d' "$FX/data/settings.conf"
        run_dl "homeid downloads; press a; press down; touch 1 180 442; shot dl-tap-edit; press start; wait 1; press down; press down; press a; wait 1; \
            touch 1 260 442; shot dl-zips; press start; wait 1; wait 300; press b; press b; wait 1; press b; press down; press down; press down; press down; press a; \
            press down; press down; touch 1 580 442; $(key 1,2 1,1 1,0) press start; touch 1 190 442; wait 1; shot dl-tap-saved"
        dlcheck "downloads: server locked saved"
        dlcheck "2 files from Test NAS into $FX/roms/gba"
        [ -f "$FX/roms/gba/Echo.gba" ] && [ -f "$FX/roms/gba/Echo Pack.zip" ] && [ ! -e "$FX/roms/gba/Echo Pack.gba" ] &&
            grep -l '^title = gba$' "$FX/data/downloads/"*.finished | xargs grep -q '^keep_zips = yes$' ||
            { echo "FAIL $size: the GBA folder's zip wasn't kept as it is (keep_zips)"; fail=1; }
        grep -q '^host = ewq$' "$FX/data/nas.conf" || { echo "FAIL $size: A Type and START Save (tapped) didn't save the server"; fail=1; }
        for s in dl-tile dl-home dl-browse dl-marked dl-target dl-unpack dl-status dl-done dl-back dl-home-done dl-reconnected dl-unreachable \
            dl-password dl-locked dl-local dl-local-delete dl-unpacked dl-local-back dl-options dl-edit dl-saved dl-progress dl-retry dl-stop \
            dl-stopped dl-cancel dl-cancelled dl-tap-edit dl-zips dl-tap-saved; do
            for p in top bot; do
                f=$OUT/$size-$s-$p.png
                [ -s "$f" ] || { echo "FAIL $size: no $f"; fail=1; continue; }
                [ "$(convert "$f" -format '%[fx:standard_deviation>0.01]' info:)" = 1 ] || { echo "FAIL $size: $f is blank"; fail=1; }
            done
        done
        dlclean
    else
        echo "SKIP $size: Downloads (no ui/banana-fetch: make -C ui banana-fetch, with libcurl, libssh2 and OpenSSL's dev packages)"
    fi
    grep 'bench:' "$log" | sed "s/^banana-ui: /$size /"
done
[ $fail = 0 ] && echo "banana-ui: all checks passed ($OUT)"
exit $fail

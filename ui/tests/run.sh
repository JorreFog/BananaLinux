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
# fileops' job on its own thread, as on the handheld (the runs below do it in sync mode): Stop in a file, a move between
# file systems stopped, quitting in the middle, long names; with ThreadSanitizer where the compiler and kernel have it.
# And on the cards' own file systems where this machine lets us make them (root, losetup, mkfs.vfat and mkfs.exfat: a
# privileged container will do): an exFAT image mounted as rcS mounts the data card and a FAT32 one as the sd2 script
# mounts the second, with those scripts' own options, for what tmpfs can't show (fileops-test's card())
FT=$OUT/fileops-test FTLOG=$OUT/fileops-test.log
O2=$OUT; [ -w /dev/shm ] && [ "$(stat -c %d /dev/shm)" != "$(stat -c %d "$OUT")" ] && O2=/dev/shm
CD="" LF="" LE=""
cards_up()
{
    why="not root"; [ "$(id -u)" = 0 ] || return 1
    for t in losetup mkfs.vfat mkfs.exfat; do why="no $t"; command -v $t >/dev/null || return 1; done
    exopt=$(sed -n 's|.*mount -t exfat -o \([^ ]*\) .* /data .*|\1|p' "$HERE/../system/rootfs/etc/init.d/rcS" | head -n 1)
    fatopt=$(sed -n 's|.*mount -t vfat -o \([^ ]*\) .*|\1|p' "$HERE/../system/rootfs/usr/libexec/banana/sd2" | head -n 1)
    why="rcS's or sd2's mount options not found"; [ -n "$exopt" ] && [ -n "$fatopt" ] || return 1
    why="the images couldn't be made"
    CD=$(mktemp -d) && mkdir "$CD/fat" "$CD/ex" && truncate -s 128M "$CD/fat.img" "$CD/ex.img" &&
        mkfs.vfat -F 32 "$CD/fat.img" >/dev/null 2>&1 && mkfs.exfat "$CD/ex.img" >/dev/null 2>&1 || return 1
    why="no loop device (not a privileged container?)"
    LF=$(losetup -f --show "$CD/fat.img" 2>/dev/null) && LE=$(losetup -f --show "$CD/ex.img" 2>/dev/null) || return 1
    why="vfat or exfat can't be mounted"
    mount -t vfat -o "$fatopt" "$LF" "$CD/fat" 2>/dev/null && mount -t exfat -o "$exopt" "$LE" "$CD/ex" 2>/dev/null
}
cards_down()
{
    [ -n "$CD" ] || return 0
    umount "$CD/fat" "$CD/ex" 2>/dev/null || true
    for l in $LF $LE; do losetup -d $l 2>/dev/null || true; done
    rm -rf "$CD" || true
    CD="" LF="" LE=""
}
trap cards_down EXIT; trap 'exit 1' INT TERM
cards_up || { cards_down; echo "note: fileops on FAT32 and exFAT skipped: $why"; }
TSAN=-fsanitize=thread
echo 'int main() { return 0; }' | ${CXX:-g++} -x c++ $TSAN -o "$OUT/tsan-probe" - 2>/dev/null && "$OUT/tsan-probe" 2>/dev/null || TSAN=
if ${CXX:-g++} -std=c++17 -O1 -g $TSAN -o "$FT" "$HERE/tests/fileops-test.cpp" "$HERE/src/fileops.cpp" -lpthread; then
    "$FT" "$OUT" "$O2" ${CD:+"$CD/fat" "$CD/ex"} > "$FTLOG" 2>&1 || { echo "FAIL: fileops-test (see $FTLOG)"; fail=1; }
    grep -q "WARNING: ThreadSanitizer" "$FTLOG" && { echo "FAIL: fileops-test: a data race (see $FTLOG)"; fail=1; }
else echo "FAIL: fileops-test doesn't build"; fail=1; fi
cards_down; trap - EXIT INT TERM
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
    # an action is A's alone: on Shut down (two up from System's first row: Hotkeys is the list's last), RIGHT twice (as a
    # held RIGHT repeats) isn't the second press; A twice then is (result 6: POWEROFF)
    alog=$OUT/$size-action.log
    "$HERE/banana-ui" --headless "$OUT/$size-" --size $size --assets "$HERE/assets/rnds" \
        --systems "$HERE/../system/rootfs/etc/banana/systems.conf" --roms "$FX/roms" --data "$FX/data" \
        --script "home 0; press start; press r; press r; press r; press r; press r; press a; press up; press up; press right; press right; press a; press a" \
        2> "$alog" || { echo "FAIL $size: banana-ui (action) exited $?"; fail=1; }
    [ "$(grep -c 'press right: settings result 0' "$alog")" = 2 ] && grep -q 'press a: settings result 6' "$alog" ||
        { echo "FAIL $size: Shut down by RIGHT, or not by A: $(grep 'press [ar]' "$alog" | tail -4 | tr '\n' ' ')"; fail=1; }
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
    pkscript="home 0; gamepanel 1 $OUT/poke.gba; gshot pk-wait; pokefeed $OUT/poke-party.txt; gshot pk-party; gtouch 100 70; gshot pk-mon; gtouch 470 200; gshot pk-mon-stats; \
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
    for s in pk-wait pk-party pk-mon pk-mon-stats pk-mon2 pk-back pk-battle pk-game pk-none zh-pk-party zh-pk-mon zh-pk-mon-stats zh-pk-battle zh-pk-game; do
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
    # Files (the shelf's tile): a card of its own (the fixture again) and a second card in /dev/shm when that is a
    # file system of its own (a move between the two then copies and deletes, as between the RG DS Plus's cards).
    # Copy to the second card, move back into gba, folders made and renamed by touch (a name refused), a paste over
    # names already there (Replace, Keep both, Skip), a copy beside itself, a card's own folder offered no Move or
    # Delete, a folder pasted into itself refused, a folder deleted with what is in it, a file too big for the room
    # refused before a byte, and a folder of 200 long names scrolled through within the settings' cache budget
    FM=$OUT/fm-$size; mlog=$OUT/$size-files.log; : > "$mlog"
    "$HERE/tests/fixture.sh" "$FM" >/dev/null
    SD2=$(mktemp -d /dev/shm/banana-sd2.XXXXXX 2>/dev/null || true)
    if [ -z "$SD2" ] || [ "$(stat -c %d "$SD2")" = "$(stat -c %d "$FM")" ]; then
        [ -n "$SD2" ] && rmdir "$SD2"
        SD2=$OUT/sd2-$size; rm -rf "$SD2"; mkdir -p "$SD2"
        echo "note $size: /dev/shm isn't a file system of its own here: the move between cards is a rename"
    fi
    run_fm() { BANANA_SD2="$SD2" "$HERE/banana-ui" --headless "$OUT/$size-" --size $size --assets "$HERE/assets/rnds" \
        --banana "$HERE/assets/banana" --systems "$HERE/../system/rootfs/etc/banana/systems.conf" --roms "$FM/roms" \
        --data "$FM/data" --script "$1" 2>> "$mlog" || { echo "FAIL $size: banana-ui (files) exited $?"; fail=1; }; }
    fcheck() { grep -q "$1" "$mlog" || { echo "FAIL $size: files: no '$1'"; fail=1; }; }
    fsame() { cmp -s "$1" "$2" || { echo "FAIL $size: files: $1 isn't $2"; fail=1; }; }
    fhas() { [ -e "$1" ] || { echo "FAIL $size: files: no $1"; fail=1; }; }
    fnot() { [ -e "$1" ] && { echo "FAIL $size: files: $1 is there"; fail=1; }; true; }
    N=$FM/roms/nds G=$FM/roms/gba
    # the cards; This card's root (data, roms) > roms (gb, gba, nds, snes) > nds; two games marked, copied, pasted on the second card
    run_fm "homeid files; press a; shot files-cards; press a; press down; press a; press down; press down; press a; shot files-folder; \
        press down; press y; press down; press y; press x; shot files-actions; press a; press b; press b; press b; press down; press a; \
        press x; press a; shot files-pasted"
    fcheck "press a: banana:files open"
    fcheck "files: Copied 2 files (8.0 KB) into Second SD card."
    fsame "$SD2/Banana Kart DS.nds" "$N/Banana Kart DS.nds"; fsame "$SD2/Monkey Puzzle League.nds" "$N/Monkey Puzzle League.nds"
    # both moved from the second card into gba (a file at a time between two file systems)
    run_fm "homeid files; press a; press down; press a; press y; press down; press y; press x; press down; press a; press b; press up; \
        press a; press down; press a; press down; press a; press x; press a; shot files-moved"
    fcheck "files: Moved 2 files (8.0 KB) into This card/roms/gba."
    fnot "$SD2/Banana Kart DS.nds"; fnot "$SD2/Monkey Puzzle League.nds"
    fsame "$G/Banana Kart DS.nds" "$N/Banana Kart DS.nds"; fsame "$G/Monkey Puzzle League.nds" "$N/Monkey Puzzle League.nds"
    # in gba: New folder "a:b" refused, then "rpg"; "x"; rpg renamed to x (taken: refused), then to y; a game renamed (.NDS)
    run_fm "homeid files; press a; press a; press down; press a; press down; press a; press x; press down; press down; press down; \
        press down; press a; $(key 2,0) press x; $(key 2,9) press x; $(key 3,4) press start; shot files-badname; press b; press b; press b; \
        $(key 1,3 1,9 2,4) press start; press x; press down; press down; press down; press down; press a; $(key 3,1) press start; \
        press up; press x; press down; press down; press a; press b; press b; press b; $(key 3,1) press start; press b; $(key 1,5) press start; \
        press down; press x; press down; press down; press a; press b; press b; press b; press x; $(key 3,5 2,2 2,1) press start"
    fcheck "files: new folder refused: A name can't have : in it"
    fcheck "files: Made the folder rpg."
    fcheck "files: rename refused: There is already something called x here."
    fcheck "files: Renamed rpg to y."
    fcheck "files: Renamed Banana Kart DS.nds to Banana Kart DS.NDS."
    fhas "$G/x"; fhas "$G/y"; fnot "$G/rpg"; fnot "$G/a:b"; fhas "$G/Banana Kart DS.NDS"
    # Peel Quest from nds into gba, where an older one is: Replace (A twice), then Keep both, then Skip
    echo old > "$G/Peel Quest.nds"
    run_fm "homeid files; press a; press a; press down; press a; press down; press down; press a; press down; press down; press down; \
        press x; press a; press b; press up; press a; press x; press a; shot files-clash; press down; press a; press a; \
        press x; press a; press a; press x; press a; press down; press down; press a"
    fsame "$G/Peel Quest.nds" "$N/Peel Quest.nds"; fsame "$G/Peel Quest (2).nds" "$N/Peel Quest.nds"; fnot "$G/Peel Quest (3).nds"
    fcheck "files: Nothing was pasted. 1 left as it was."
    # roms: gb (a system's folder) offered no Move, Rename or Delete; gba copied and pasted into itself: refused; in gba
    # the folder old deleted with what is in it; Orchard Tactics copied beside itself
    mkdir -p "$G/old/sub"; echo f > "$G/old/sub/f"; echo g > "$G/old/g"
    run_fm "homeid files; press a; press a; press down; press a; press x; press b; press down; press x; press a; press a; press x; press a; \
        press x; press down; press down; press down; press down; press a; press a; press down; press down; press down; press down; press x; \
        press down; press a; press x; press a"
    fcheck "files: actions for gb: Copy, New folder, Mark all"
    fcheck "files: paste refused: A folder can't go into itself"
    fcheck "files: Deleted 1 folder."
    fnot "$G/old"; fhas "$N/Peel Quest.nds"
    fsame "$G/Orchard Tactics (2).gba" "$G/Orchard Tactics.gba"
    # a file bigger than the second card's room: refused before a byte is written
    truncate -s 1T "$FM/roms/gb/big.gb"
    run_fm "homeid files; press a; press a; press down; press a; press a; press x; press a; press b; press b; press b; press down; press a; \
        press x; press a"
    fcheck "files: Not enough room: "
    fnot "$SD2/big.gb"; [ -z "$(ls -A "$SD2")" ] || { echo "FAIL $size: files: left on the second card: $(ls -A "$SD2")"; fail=1; }
    rm -f "$FM/roms/gb/big.gb"
    # 200 long names: the settings' cache stays within its budget, banana-ui's memory within 64 MB of where it was
    mkdir -p "$FM/long"
    for i in $(seq 100 299); do : > "$FM/long/$i Some Rather Long Game Title With A Subtitle - The Revenge of the Long File Names (Europe) (En,Fr,De,Es,It) (Rev 1).nds"; done
    steps=""; for i in $(seq 200); do steps="$steps press down; frame;"; done
    run_fm "homeid files; press a; press a; press down; press a; mem; $steps mem; shot files-long"
    awk '/mem: RSS/ { rss[++n] = $3; for (i = 1; i <= NF; i++) if ($i == "settings") { used = $(i + 4); budget = $(i + 8) } }
        END { if (n < 2 || used == "" || used + 0 > budget + 0 || rss[2] - rss[1] > 65536) { print "FAIL: files: cache " used " of " budget " MB, RSS " rss[1] " -> " rss[2] " kB"; exit 1 } }' "$mlog" \
        || { echo "FAIL $size: files: memory, see $mlog"; fail=1; }
    # a fresh card for the rest (fm2): a job on its thread, its page shot while it works and Y twice stopping it; RIGHT
    # never choosing an action; a 244-byte name copied; an empty folder copied and a folder merged said as one folder;
    # Mark all in roms moving or deleting only the user's own folder; SELECT leaving a rename; a Windows device name
    FM=$OUT/fm2-$size; mlog=$OUT/$size-files2.log; : > "$mlog"
    "$HERE/tests/fixture.sh" "$FM" >/dev/null
    rm -rf "$SD2"/* 2>/dev/null || true
    N=$FM/roms/nds G=$FM/roms/gba
    # in gba: X, down to Delete, RIGHT twice (as a held RIGHT repeats: a page down, to Mark all); B, RIGHT twice in the
    # folder (pages too): nothing deleted or copied
    run_fm "homeid files; press a; press a; press down; press a; press down; press a; press x; press down; press down; press down; \
        press right; press right; press b; press right; press right"
    fhas "$G/Orchard Tactics.gba"; grep -q "files: Deleted\|files: copy" "$mlog" && { echo "FAIL $size: files: RIGHT chose an action"; fail=1; }
    # Big.gba (256 MB, sparse) copied beside itself on its thread, slower than a card (100 ms a MiB: 25 s, longer than
    # a shot takes even in a sanitizer's build; Stop ends it within a block): its page shot while it works (the page
    # comes 300 ms after the first update since the paste: frame, then wait), Y twice
    truncate -s 256M "$G/Big.gba"
    BANANA_FILES_ASYNC=1 BANANA_FILES_PACE=100 run_fm "homeid files; press a; press a; press down; press a; press down; press a; press x; press a; \
        press x; press a; frame; wait 400; sleep 30; shot files-job; press y; press y; filesdone"
    fcheck "files: Stopped: 0 of 1 done."
    fnot "$G/Big (2).gba"; [ -z "$(ls -A "$G" | grep '^\.banana-')" ] || { echo "FAIL $size: files: a hidden copy left in gba"; fail=1; }
    rm -f "$G/Big.gba"
    long=$(printf 'A%.0s' $(seq 241)).gb; : > "$FM/roms/gb/$long"
    run_fm "homeid files; press a; press a; press down; press a; press a; press x; press a; press b; press b; press b; press down; press a; \
        press x; press a"
    fcheck "files: Copied 1 file into Second SD card."; fhas "$SD2/$long"
    mkdir "$G/empty"; mkdir -p "$N/pack" "$G/pack"; : > "$N/pack/a.nds"; : > "$N/pack/b.nds"
    run_fm "homeid files; press a; press a; press down; press a; press down; press a; press x; press a; press b; press b; press b; \
        press down; press a; press x; press a"
    fcheck "files: Copied 1 folder into Second SD card."; fhas "$SD2/empty"
    run_fm "homeid files; press a; press a; press down; press a; press down; press down; press a; press down; press x; press down; press a; \
        press b; press up; press a; press x; press a; press down; press a; press a"
    fcheck "files: Moved 1 folder into This card/roms/gba."; fhas "$G/pack/a.nds"; fnot "$N/pack"
    mkdir "$FM/roms/my-stuff"
    run_fm "homeid files; press a; press a; press down; press a; press x; press down; press down; press a; press x; press down; press down; \
        press a; press a"
    fcheck "files: actions for 5 items: Copy, Move, Delete, New folder, Unmark all"
    fcheck "files: Deleted 1 folder."; fnot "$FM/roms/my-stuff"; fhas "$FM/roms/gb"
    # (SELECT on the keyboard: back to the actions, the cursor on Rename; two down is New folder)
    run_fm "homeid files; press a; press a; press down; press a; press down; press a; press x; press down; press down; press a; press select; \
        press down; press down; press a; $(key 3,5 1,6 2,8) press start"
    fhas "$G/empty"; grep -q "files: Renamed" "$mlog" && { echo "FAIL $size: files: SELECT renamed"; fail=1; }
    fcheck "files: new folder refused: Windows keeps the name nul"
    # a swipe up that starts on a row (the fourth) scrolls a page (the sixth chosen), not a tap on that row
    mkdir -p "$FM/swipe"; for i in 00 01 02 03 04 05 06 07 08 09 10 11; do : > "$FM/swipe/f$i"; done
    run_fm "homeid files; press a; press a; press down; press down; press a; swipe 300 300 300 100; press x"
    fcheck "files: actions for f05:"
    # LEFT and RIGHT turn a page (5 rows), L and R go to the first and last rows: in swipe, from f00, RIGHT twice is f10,
    # R f11, LEFT f06, L f00 (X on each: its actions)
    run_fm "homeid files; press a; press a; press down; press down; press a; press right; press right; press x; press b; press r; press x; \
        press b; press left; press x; press b; press l; press x"
    for f in f10 f11 f06 f00; do fcheck "files: actions for $f:"; done
    # a systems.conf of the user's made before Files (no [files]): the tile is there all the same
    awk '/^\[files\]/ { skip = 1; next } /^\[/ { skip = 0 } !skip' "$HERE/../system/rootfs/etc/banana/systems.conf" > "$OUT/systems-nofiles.conf"
    nlog=$OUT/$size-nofiles.log
    "$HERE/banana-ui" --headless "$OUT/$size-" --size $size --assets "$HERE/assets/rnds" --banana "$HERE/assets/banana" \
        --systems "$OUT/systems-nofiles.conf" --roms "$FM/roms" --data "$FM/data" --script "homeid files; press a" 2> "$nlog" \
        || { echo "FAIL $size: banana-ui (no [files]) exited $?"; fail=1; }
    grep -q "press a: banana:files open" "$nlog" || { echo "FAIL $size: no Files tile without [files] in systems.conf"; fail=1; }
    for s in files-cards files-folder files-actions files-pasted files-moved files-badname files-clash files-long files-job; do
        for p in top bot; do
            f=$OUT/$size-$s-$p.png
            [ -s "$f" ] || { echo "FAIL $size: no $f"; fail=1; continue; }
            [ "$(convert "$f" -format '%[fx:standard_deviation>0.01]' info:)" = 1 ] || { echo "FAIL $size: $f is blank"; fail=1; }
        done
    done
    case $SD2 in /dev/shm/*) rm -rf "$SD2" ;; esac
    # Files' servers (settings_servers.cpp), with banana-fetch itself (built when this computer has its libraries; the
    # top Makefile's test does it) and its test protocol, "dir", standing in for a server (a folder here; only with
    # BANANA_FETCH_TEST=1). Headless, the menu runs banana-fetch's runner to the end at once. A server's folder browsed,
    # two files marked and downloaded into the folder their type says (the zip unpacked, as asked; RIGHT on the server
    # logs in to nothing); a folder made and chosen through Files' own navigation (and a choice given up on); a file
    # copied on the server and pasted into a new folder of the card (not into banana/); an archive on the card unpacked
    # where it is; an option changed, a server added, a locked server's password typed; the connection lost with Wi-Fi
    # off (the folder back once it connects, B out of it meanwhile); a job's runner at work beside the menu (its
    # progress, the retry's countdown, X Stop and Y Cancel tapped); a server's folder of long names within the cache
    if [ -x "$HERE/banana-fetch" ]; then
        SV=$OUT/sv-$size; slog=$OUT/$size-servers.log; NAS=$OUT/nas-$size; SRUN=$OUT/srun-$size
        "$HERE/tests/fixture.sh" "$SV" >/dev/null
        rm -rf "$NAS" "$SRUN"; mkdir -p "$NAS/Games/nds" "$NAS/Games/gba" "$SRUN"; : > "$slog"
        head -c 70000 /dev/urandom > "$NAS/Games/nds/Alpha Quest.nds"
        head -c 1000 /dev/zero > "$NAS/Games/gba/Echo.gba"
        python3 - "$NAS/Games/nds/Beta Racer.zip" "$SV/Delta.zip" <<'PY'
import sys, zipfile
with zipfile.ZipFile(sys.argv[1], 'w', zipfile.ZIP_DEFLATED) as z:
    z.writestr('Beta Racer.nds', b'B' * 50000)
with zipfile.ZipFile(sys.argv[2], 'w', zipfile.ZIP_DEFLATED) as z:
    z.writestr('Delta/Delta.gba', b'D' * 30000)
PY
        printf '[locked]\nname = Locked\nprotocol = dir\nhost = %s\nuser = u\nremember = no\n\n[testnas]\nname = Test NAS\nprotocol = dir\nhost = %s\nguest = yes\nstart = /Games\n' \
            "$NAS" "$NAS" > "$SV/data/nas.conf"
        SV_SD2=""  # (the second card: set only where a scenario wants it)
        run_sv() { BANANA_SD2="$SV_SD2" BANANA_FETCH="$HERE/banana-fetch" BANANA_FETCH_TEST=1 BANANA_FETCH_TEST_FAST=1 BANANA_FETCH_TEST_PASSWORD=qwe BANANA_RUN="$SRUN" \
            "$HERE/banana-ui" --headless "$OUT/$size-" --size $size --assets "$HERE/assets/rnds" --banana "$HERE/assets/banana" \
            --systems "$HERE/../system/rootfs/etc/banana/systems.conf" --roms "$SV/roms" --data "$SV/data" --script "$1" 2>> "$slog" ||
            { echo "FAIL $size: banana-ui (servers) exited $?"; fail=1; }; }
        scheck() { grep -q "$1" "$slog" || { echo "FAIL $size: servers: no '$1'"; fail=1; }; }
        # (headless, a press the menu takes isn't followed by its update: "wait 1" lets banana-fetch's answer in)
        # one card: Files opens on its list all the same (the servers are in it), and B at the card's root comes back to
        # it; B there closes Files
        n=$(wc -l < "$slog")
        run_sv "homeid files; press a; shot sv-one-root; press a; shot sv-one-card; press b; shot sv-one-back; press b"
        tail -n +$((n + 1)) "$slog" | grep '^banana-ui: press b' | head -n 1 | grep -q 'settings result' &&
            tail -n +$((n + 1)) "$slog" | grep '^banana-ui: press b' | sed -n 2p | grep -q 'settings closed' ||
            { echo "FAIL $size: servers: with one card, B at its root didn't come back to Files' list"; fail=1; }
        # Files' list: This card, Locked, Test NAS (Add a server and Download options are START's). RIGHT on Test NAS twice
        # (a page down: it stays on the last row, and logs in to nothing), then A: its
        # /Games (gba, nds); in nds Alpha Quest.nds and Beta Racer.zip marked, X, Download: the folder by type (roms/nds),
        # Unpack archives (No: the DS plays zips) turned to Yes; START. B on the job's page comes back to nds, B up to
        # /Games, / and Files' list
        run_sv "homeid files; press a; shot sv-root; press down; press down; press right; press right; press a; wait 1; shot sv-browse; \
            press down; press a; wait 1; press y; press down; press y; press x; shot sv-actions; press a; press down; press right; shot sv-target; \
            press start; wait 1; wait 300; shot sv-status; fetchdone; shot sv-done; press b; wait 1; press b; wait 1; press b; wait 1; press b; shot sv-root-done"
        [ "$(grep -c 'servers: connected to Test NAS' "$slog")" = 1 ] || { echo "FAIL $size: servers: RIGHT on a server logged in"; fail=1; }
        scheck "servers: connected to Test NAS (DIR), home /"
        scheck "servers: /Games/nds: 2 entries"
        scheck "2 files from Test NAS into $SV/roms/nds$"
        scheck "notice: Downloaded: 2 files from Test NAS"
        scheck "servers: /: 1 entries"
        cmp -s "$NAS/Games/nds/Alpha Quest.nds" "$SV/roms/nds/Alpha Quest.nds" || { echo "FAIL $size: servers: Alpha Quest.nds wasn't downloaded"; fail=1; }
        [ -f "$SV/roms/nds/Beta Racer.nds" ] && cmp -s "$NAS/Games/nds/Beta Racer.zip" "$SV/roms/nds/Beta Racer.zip" ||
            { echo "FAIL $size: servers: Beta Racer.zip wasn't downloaded and unpacked, the zip kept"; fail=1; }
        printf 'target = /Games/nds\tdata:roms/nds\n' | grep -qxFf - "$SV/data/nas.conf" || { echo "FAIL $size: servers: the folder wasn't remembered"; fail=1; }
        # Files' list: Last download, This card, Locked, Test NAS... /Games/gba's Echo.gba, Download: the folder roms/gba;
        # A on it: Files on roms/gba, X makes Hacks (opened at once, empty), A chooses it; START. Then the same again: the folder
        # is Hacks (last time), and B from it up to the cards and once more gives the choice up
        run_sv "homeid files; press a; press down; press down; press down; press a; wait 1; press a; wait 1; press x; press a; press a; shot sv-pick; \
            press x; type Hacks; press start; shot sv-pick-new; press a; shot sv-picked; press start; wait 1; wait 300; fetchdone; press b; wait 1; \
            press x; press a; press a; press b; press b; press b; press b; press b; shot sv-pick-cancelled"
        scheck "files: Made the folder Hacks."
        scheck "files: folder chosen: $SV/roms/gba/Hacks$"
        scheck "1 files from Test NAS into $SV/roms/gba/Hacks$"
        scheck "files: no folder chosen"
        [ -f "$SV/roms/gba/Hacks/Echo.gba" ] || { echo "FAIL $size: servers: Echo.gba isn't in the folder chosen"; fail=1; }
        printf 'target = /Games/gba\tdata:roms/gba/Hacks\n' | grep -qxFf - "$SV/data/nas.conf" || { echo "FAIL $size: servers: the folder chosen wasn't remembered"; fail=1; }
        # the second card in (Files' list: Last download, This card, Second SD card, Locked, Test NAS...): Echo.gba again,
        # its folder Hacks as last time, chosen again in Files by A on its file (Echo.gba: A takes the folder shown, as Y),
        # and the Card row turned to the second card: the same folder there (made by the
        # download). The page's job done, START closes Files before the runner's word comes: its banner over the menu
        SV_SD2=$OUT/svsd2-$size; rm -rf "$SV_SD2"; mkdir -p "$SV_SD2"
        n=$(wc -l < "$slog")
        run_sv "homeid files; press a; press down; press down; press down; press down; press a; wait 1; press a; wait 1; press x; press a; \
            press a; press a; press down; press right; shot sv-card; press start; wait 1; wait 300; press start; shot sv-closed; fetchdone; shot sv-banner"
        scheck "1 files from Test NAS into $SV_SD2/roms/gba/Hacks$"
        tail -n +$((n + 1)) "$slog" | grep -q "files: folder chosen: $SV/roms/gba/Hacks$" ||
            { echo "FAIL $size: servers: A on a file didn't choose the folder shown"; fail=1; }
        [ -f "$SV_SD2/roms/gba/Hacks/Echo.gba" ] || { echo "FAIL $size: servers: Echo.gba isn't on the second card"; fail=1; }
        tail -n +$((n + 1)) "$slog" | grep -A 3 'press start: settings closed' | grep -q 'notice: Downloaded: Echo.gba' ||
            { echo "FAIL $size: servers: no banner for a job done with Files closed"; fail=1; }
        cmp -s "$OUT/$size-sv-closed-top.png" "$OUT/$size-sv-banner-top.png" && { echo "FAIL $size: servers: the banner isn't drawn over the menu"; fail=1; }
        printf 'target = /Games/gba\tsd2:roms/gba/Hacks\n' | grep -qxFf - "$SV/data/nas.conf" || { echo "FAIL $size: servers: the second card's folder wasn't remembered"; fail=1; }
        SV_SD2=""
        # Alpha Quest.nds copied (X, Copy) on the server; up and out to This card: Paste here in its banana folder (here
        # data) refused; on roms, X, New folder Downloads; in it, X, Paste here: the Download page on it, START
        run_sv "homeid files; press a; press down; press down; press down; press a; wait 1; press down; press a; wait 1; press a; press down; press a; \
            shot sv-copied; press b; wait 1; press b; wait 1; press b; wait 1; press up; press up; press a; press a; press x; press a; shot sv-banana; \
            press b; press down; press x; press down; press down; press a; type Downloads; press start; press a; press x; shot sv-paste; press a; \
            shot sv-paste-target; press start; wait 1; wait 300; fetchdone"
        scheck "servers: copy 1 from Test NAS /Games/nds"
        scheck "files: paste refused: Not into banana/"
        scheck "files: download 1 from Test NAS into $SV/Downloads$"
        cmp -s "$NAS/Games/nds/Alpha Quest.nds" "$SV/Downloads/Alpha Quest.nds" || { echo "FAIL $size: servers: the paste didn't download into Downloads"; fail=1; }
        ls "$SV/data" | grep -q 'Alpha' && { echo "FAIL $size: servers: something went into banana/"; fail=1; }
        # This card's root (data, Downloads, roms, Delta.zip): Delta.zip, X, Unpack...: the folder it is in, Delete the
        # archive after (Options' Ask) to Yes, START: its one folder unwrapped; B back to the card's folder, without it
        run_sv "homeid files; press a; press down; press a; press down; press down; press down; press x; press down; press down; press down; press down; \
            shot sv-unpack-action; press a; shot sv-unpack; press down; press right; press start; wait 1; wait 300; fetchdone; shot sv-unpacked; press b; shot sv-unpack-back"
        scheck "1 archive unpacked into $SV$"
        scheck "notice: Unpacked Delta.zip"
        [ "$(cat "$SV/Delta.gba" 2>/dev/null | wc -c)" = 30000 ] || { echo "FAIL $size: servers: Delta.zip wasn't unpacked on the card"; fail=1; }
        [ -e "$SV/Delta.zip" ] && { echo "FAIL $size: servers: Delta.zip wasn't deleted, as asked"; fail=1; }
        # two archives marked among other files (Packs: Notes.txt, One.zip, Two.zip): Unpack... takes the archives, one job
        mkdir -p "$SV/Packs"; echo notes > "$SV/Packs/Notes.txt"
        python3 - "$SV/Packs" <<'PY'
import sys, zipfile
for n in ('One', 'Two'):
    with zipfile.ZipFile(sys.argv[1] + '/' + n + '.zip', 'w', zipfile.ZIP_DEFLATED) as z:
        z.writestr(n + '.gba', n[0].encode() * 1000)
PY
        run_sv "homeid files; press a; press down; press a; press down; press down; press a; press down; press y; press down; press y; press x; \
            shot sv-multi-actions; press down; press down; press down; press a; shot sv-multi; press start; wait 1; wait 300; fetchdone; press b; shot sv-multi-back"
        scheck "files: actions for 2 items: Copy, Move, Delete, Unpack..., New folder, Mark all"
        scheck "2 archives unpacked into $SV/Packs$"
        scheck "notice: Unpacked 2 archives"
        [ "$(cat "$SV/Packs/One.gba" "$SV/Packs/Two.gba" 2>/dev/null | wc -c)" = 2000 ] || { echo "FAIL $size: servers: the two archives weren't unpacked"; fail=1; }
        # an archive with a password (7zz's): Unpack... looks into it, the keyboard asks; a wrong one asks again; the right
        # one unpacks it. Neither is in the log
        if command -v 7zz >/dev/null; then
            cp "$HERE/tests/archives/encrypted.7z" "$SV/Packs/"
            run_sv "homeid files; press a; press down; press a; press down; press down; press a; press x; press down; press down; press down; press down; \
                press a; press start; wait 1; shot sv-arcpw; type wrong; press start; wait 1; shot sv-arcpw-wrong; type Secret123; press start; wait 1; \
                wait 300; fetchdone; shot sv-arcpw-done"
            scheck "servers: inspect: password: "
            scheck "servers: inspect: wrong_password: "
            scheck "1 archive unpacked into $SV/Packs$"
            [ "$(cat "$SV/Packs/game.nds" 2>/dev/null | wc -c)" = 32768 ] || { echo "FAIL $size: servers: the archive with a password wasn't unpacked"; fail=1; }
            grep -q 'Secret123\|wrong$' "$slog" && { echo "FAIL $size: servers: a password is in the log"; fail=1; }
            ls "$SRUN"/*.apw >/dev/null 2>&1 && { echo "FAIL $size: servers: the archive's password file stayed in /run"; fail=1; }
            for s in sv-arcpw sv-arcpw-wrong sv-arcpw-done; do for p in top bot; do [ -s "$OUT/$size-$s-$p.png" ] || { echo "FAIL $size: no $s"; fail=1; }; done; done
        else
            echo "SKIP $size: servers: an archive with a password (no 7zz)"
        fi
        # START on Files' list: its settings (Add a server, Download options). Download options: Unpack after a download to
        # Always; B back to the settings, on Download options; Add a server: SFTP, its address typed (qwe), START saves it;
        # Locked: its password typed (qwe)
        run_sv "homeid files; press a; press start; shot sv-settings; press down; press a; press right; shot sv-options; press b; press up; press a; \
            press down; press right; press down; press a; type qwe; press start; shot sv-edit; press start; wait 1; shot sv-saved"
        grep -q '^files_unpack = always$' "$SV/data/settings.conf" || { echo "FAIL $size: servers: Download options didn't keep Unpack after a download"; fail=1; }
        grep -q '^host = qwe$' "$SV/data/nas.conf" && grep -q '^protocol = sftp$' "$SV/data/nas.conf" ||
            { echo "FAIL $size: servers: the server added isn't in nas.conf"; fail=1; }
        scheck "servers: server .* saved"
        run_sv "homeid files; press a; press down; press down; press a; shot sv-password; type qwe; press start; wait 1; shot sv-locked"
        scheck "servers: connected to Locked (DIR), home /"
        # the lid shut in a server's folder: Wi-Fi unloaded, back only a while after the lid opens. In nds the connection
        # is gone (the 3rd listing) and connecting again fails (the 2nd connection): SELECT reads the folder in vain, then
        # B connects again by itself and goes up to /Games. And with B first: Files' list, not nds for good
        export BANANA_FETCH_TEST_DROPLIST=3 BANANA_FETCH_TEST_OFFLINE=2
        n=$(wc -l < "$slog")
        run_sv "homeid files; press a; press down; press down; press down; press down; press a; wait 1; press down; press a; wait 1; \
            press select; wait 1; press b; wait 1; shot sv-reconnected"
        tail -n +$((n + 1)) "$slog" | grep -A 4 "servers: list: network: " | grep -q "servers: /Games: 2 entries" ||
            { echo "FAIL $size: servers: the folder didn't come back after connecting again failed"; fail=1; }
        n=$(wc -l < "$slog")
        run_sv "homeid files; press a; press down; press down; press down; press down; press a; wait 1; press down; press a; wait 1; \
            press b; wait 1; shot sv-unreachable; press b"
        tail -n +$((n + 1)) "$slog" | grep -A 2 "servers: list: network: " | grep -q "press b: settings closed" ||
            { echo "FAIL $size: servers: B didn't get out of the folder of a server that can't be reached"; fail=1; }
        unset BANANA_FETCH_TEST_DROPLIST BANANA_FETCH_TEST_OFFLINE
        # a job's runner at work beside the menu (started here, a piece every 100 ms, the connection cut at 1 MB): its row
        # first in Files' list; the job's page while it downloads (its two bars) and while it waits to try again; X Stop
        # tapped twice stops it, its part and its job kept (the part shown as downloading, nothing to do with it); then Y
        # Cancel tapped twice deletes both. ("sleep": real time, for the runner)
        head -c 3000000 /dev/urandom > "$NAS/Games/nds/Gamma Big.nds"
        JOB=$SV/data/downloads/20990101-000000-1.job
        printf 'title = Gamma Big.nds\nserver = testnas\nunpack = no\ntemp_root = %s\nget\t3000000\t/Games/nds/Gamma Big.nds\t%s\n' "$SV" "$SV/roms/nds/Gamma Big.nds" > "$JOB"
        BANANA_DATA="$SV/data" BANANA_RUN="$SRUN" BANANA_FETCH_TEST=1 BANANA_FETCH_TEST_DELAY=100 BANANA_FETCH_TEST_CUT=1000000 "$HERE/banana-fetch" run 2>> "$slog" & rp=$!
        upto() { i=0; until grep -q "^$1 " "$SRUN/fetch/status" 2>/dev/null || [ $i -ge 100 ]; do sleep 0.1; i=$((i + 1)); done; }
        upto get
        run_sv "homeid files; press a; shot sv-job-row; press a; shot sv-progress"
        upto retry
        run_sv "homeid files; press a; press a; shot sv-retry; touch 1 180 442; shot sv-stop; touch 1 180 442; sleep 1500; wait 600; shot sv-stopped"
        wait $rp || true
        grep -q '^stopped' "$SRUN/fetch/status" && [ -f "$JOB" ] && [ -s "$SV/roms/nds/Gamma Big.nds.part" ] ||
            { echo "FAIL $size: servers: X Stop (tapped) didn't stop the download, its part and job kept: $(cat "$SRUN/fetch/status")"; fail=1; }
        # (This card: data, Downloads, Packs, roms; roms: gb, gba, nds; nds: media, Alpha Quest.nds, Banana Kart DS.nds,
        # Beta Racer.nds, Beta Racer.zip, Gamma Big.nds.part)
        run_sv "homeid files; press a; press down; press a; press down; press down; press down; press a; press down; press down; press a; \
            press down; press down; press down; press down; press down; shot sv-part; press x"
        scheck "files: actions for This card/roms/nds: New folder, Mark all"
        run_sv "homeid files; press a; press a; touch 1 180 442; shot sv-cancel; touch 1 180 442; wait 1; shot sv-cancelled"
        scheck "servers: $JOB cancelled"
        [ ! -e "$JOB" ] && [ ! -e "$SV/roms/nds/Gamma Big.nds.part" ] || { echo "FAIL $size: servers: Y Cancel (tapped) left the job or its part"; fail=1; }
        cmp -s "$OUT/$size-sv-progress-top.png" "$OUT/$size-sv-retry-top.png" && { echo "FAIL $size: servers: the retry's countdown looks like the progress"; fail=1; }
        # a server's folder of long names scrolled through (DOWN, then a frame, as the device draws it), on a card of its
        # own: the settings' texture cache keeps to its budget, and banana-ui's memory with it
        NAMES=$OUT/names-$size; rm -rf "$NAMES"; mkdir -p "$NAMES/nas" "$NAMES/data" "$NAMES/run" "$NAMES/roms"
        i=1; while [ $i -le 200 ]; do : > "$NAMES/nas/$i - Mystery Dungeon - Explorers of the Long Name, Special Edition (Europe) (En,Fr,De,Es,It) (Rev 1).nds"; i=$((i + 1)); done
        printf '[names]\nname = Names\nprotocol = dir\nhost = %s\nguest = yes\n' "$NAMES/nas" > "$NAMES/data/nas.conf"
        S=""; i=1; while [ $i -lt 200 ]; do S="$S press down; frame;"; i=$((i + 1)); done
        n=$(wc -l < "$slog")
        BANANA_FETCH="$HERE/banana-fetch" BANANA_FETCH_TEST=1 BANANA_RUN="$NAMES/run" "$HERE/banana-ui" --headless "$OUT/$size-" --size $size \
            --assets "$HERE/assets/rnds" --systems "$HERE/../system/rootfs/etc/banana/systems.conf" --roms "$SV/roms" --data "$NAMES/data" \
            --script "homeid files; press a; press down; press a; wait 1; frame; mem; $S mem; shot sv-names" 2>> "$slog" ||
            { echo "FAIL $size: banana-ui (a server's folder of long names) exited $?"; fail=1; }
        scheck "servers: /: 200 entries"
        m0=$(tail -n +$((n + 1)) "$slog" | grep 'mem: RSS' | head -n 1); m1=$(tail -n +$((n + 1)) "$slog" | grep 'mem: RSS' | tail -n 1)
        if [ -z "$m0" ] || [ "$m0" = "$m1" ]; then echo "FAIL $size: servers: no mem lines from the folder of long names"; fail=1
        else
            kb() { echo "$1" | sed 's/.*RSS[^0-9]*\([0-9]*\) kB.*/\1/'; }
            grown=$(( ($(kb "$m1") - $(kb "$m0")) / 1024 ))
            [ $grown -lt 64 ] || { echo "FAIL $size: servers: 200 long names scrolled through took $grown MB more"; fail=1; }
            echo "$m1" | awk '{ sub(/.*settings cache /, ""); exit !($3 <= $7) }' ||
                { echo "FAIL $size: servers: the settings' texture cache went past its budget: ${m1##*settings cache }"; fail=1; }
        fi
        for s in sv-one-root sv-one-card sv-one-back sv-card sv-closed sv-banner sv-multi-actions sv-multi sv-multi-back sv-root sv-browse sv-actions sv-target sv-status sv-done sv-root-done sv-pick sv-pick-new sv-picked sv-pick-cancelled sv-copied \
            sv-banana sv-paste sv-paste-target sv-unpack-action sv-unpack sv-unpacked sv-unpack-back sv-settings sv-options sv-edit sv-saved sv-password \
            sv-locked sv-reconnected sv-unreachable sv-job-row sv-progress sv-retry sv-stop sv-stopped sv-part sv-cancel sv-cancelled sv-names; do
            for p in top bot; do
                f=$OUT/$size-$s-$p.png
                [ -s "$f" ] || { echo "FAIL $size: no $f"; fail=1; continue; }
                [ "$(convert "$f" -format '%[fx:standard_deviation>0.01]' info:)" = 1 ] || { echo "FAIL $size: $f is blank"; fail=1; }
            done
        done
    else
        echo "SKIP $size: Files' servers (no ui/banana-fetch: make -C ui banana-fetch, with libcurl, libssh2 and OpenSSL's dev packages)"
    fi
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

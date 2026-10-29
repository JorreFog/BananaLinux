# The menu's UX checks (sourced by run.sh once per panel size: $size, $OUT, $HERE and $fail are its). Each runs
# banana-ui on the test library with a script of its own and checks the log, the files, or regions of the shots.
ux_k=$(echo "$size" | awk -Fx '{ print $2 / 480 }')
# a test library of its own, as the fixture makes it (the checks before changed the shared one)
UXFX=$OUT/ux-fixture-$size
rm -rf "$UXFX"; "$HERE/tests/fixture.sh" "$UXFX" >/dev/null
# ux_run <name> <script> [env...]: the log in $OUT/$size-<name>.log
ux_run() {
    n=$1; scr=$2; shift 2
    env "$@" "$HERE/banana-ui" --headless "$OUT/$size-" --size $size --assets "$HERE/assets/rnds" --splash "$HERE/assets/splash" \
        --banana "$HERE/assets/banana" --systems "$HERE/../system/rootfs/etc/banana/systems.conf" --roms "$UXFX/roms" --data "$UXFX/data" \
        --script "$scr" 2> "$OUT/$size-$n.log" || { echo "FAIL $size: banana-ui ($n) exited $?"; fail=1; }
}
ux_check() { grep -q "$2" "$OUT/$size-$1.log" || { echo "FAIL $size ($1): no '$2'"; fail=1; }; }
# ux_std <png> <w> <h> <x> <y>: the standard deviation of a region given in px of a 640x480 panel
ux_std() { convert "$1" -crop "$(awk -v k=$ux_k -v w=$2 -v h=$3 -v x=$4 -v y=$5 'BEGIN { printf "%dx%d+%d+%d", w * k, h * k, x * k, y * k }')" -format '%[fx:standard_deviation]' info: 2>/dev/null; }
# ux_diff <a.png> <b.png>: how many pixels differ
ux_diff() { compare -metric AE "$1" "$2" null: 2>&1 | cut -d' ' -f1; }
# ux_lt <value> <limit>: true when value < limit
ux_lt() { awk -v a="$1" -v b="$2" 'BEGIN { exit !(a < b) }'; }

# the home row has two ends (Pixel): on the first system nothing is drawn left of it (the track alone); on the last,
# nothing right of it; left on the first still goes to the last
ux_run row "home 0; wait 2400; shot ux-row-first; press left; wait 2400; shot ux-row-last"
ux_check row "press left: used, view 0 sys 6"     # (six systems and Bshare: Ports with PortMaster alone is off the row)
ux_lt "$(ux_std "$OUT/$size-ux-row-first-bot.png" 210 80 30 200)" 0.08 || { echo "FAIL $size: something drawn left of the first system"; fail=1; }
ux_lt "$(ux_std "$OUT/$size-ux-row-last-bot.png" 210 80 400 200)" 0.08 || { echo "FAIL $size: something drawn right of the last system"; fail=1; }

# the box art's frame (Pixel): it fits the picture (owner, 0.5.0: a square frame stood taller than a DS box): a tall
# cover's frame top is elsewhere than a wide one's, and a game without one keeps the square
convert -size 260x420 "gradient:#3d9e9a-black" "$UXFX/roms/nds/media/box/Peel Quest.png"
ux_run cover "lib 0 0; wait 2400; shot ux-cover-wide; press right; wait 2400; shot ux-cover-none; press right; wait 2400; shot ux-cover-tall"
ux_strip() { convert "$1" -crop "$(awk -v k=$ux_k -v y=$2 -v h=$3 'BEGIN { printf "%dx%d+%d+%d", 250 * k, h * k, 38 * k, y * k }')" +repage "$4"; }
for s in wide none tall; do
    ux_strip "$OUT/$size-ux-cover-$s-top.png" 146 9 "$OUT/$size-ux-cover-$s-edge.png"
    ux_strip "$OUT/$size-ux-cover-$s-top.png" 380 14 "$OUT/$size-ux-cover-$s-shadow.png"
done
[ "$(ux_diff "$OUT/$size-ux-cover-wide-edge.png" "$OUT/$size-ux-cover-tall-edge.png")" = 0 ] && { echo "FAIL $size: the frame didn't follow the cover's shape"; fail=1; }

# the text on the accent (Auto): dark or light by contrast, the second line in the accent's hue and readable (4.5:1);
# blue's sublabel is a blue, not the brown that suited yellow only. Light: white text, the plate's START too
ux_run ink "inkon f0b429; inkon 4a9fe0; inkon e8574d; inkon 1a2a6c"
ux_check ink "inkon f0b429: ink 17161c"
ux_check ink "inkon 1a2a6c: ink ffffff"
grep "^banana-ui: inkon" "$OUT/$size-ink.log" | awk '{ gsub(/[(:]/, " "); for (i = 1; i <= NF; i++) if ($i ~ /^[0-9.]+$/ && $(i+1) == "1)") if ($i + 0 < 4.5) bad = 1 } END { exit bad }' \
    || { echo "FAIL $size: a text on the accent under 4.5:1: $(grep inkon "$OUT/$size-ink.log" | tr '\n' ' ')"; fail=1; }
sub=$(sed -n 's/.*inkon 4a9fe0: .* sub \([0-9a-f]*\).*/\1/p' "$OUT/$size-ink.log")
[ -n "$sub" ] && [ $((0x${sub#????})) -gt $((0x${sub%????})) ] || { echo "FAIL $size: blue's sublabel isn't blue: $sub"; fail=1; }
sed -i 's/^accent_text = .*//' "$UXFX/data/settings.conf"; echo "accent_text = light" >> "$UXFX/data/settings.conf"
ux_run ink-light "inkon f0b429; home 0; wait 2400; shot ux-ink-light"
ux_check ink-light "inkon f0b429: ink ffffff"
sed -i 's/^accent_text = .*//' "$UXFX/data/settings.conf"

# L/R go a page: five games in a list (the shoulders name the games they land on), five rows in a settings list
# (Display & sound: R twice from Theme is Clock, Right sets 12-hour)
ux_run page "lib 0 0; wait 2400; shot ux-page; press r; press l; press start; press a; press r; press r; press right; press b; press b"
ux_check page "press r: used, view 1 sys 0 game 4"
ux_check page "press l: used, view 1 sys 0 game 0"
grep -q '^clock = 12h' "$UXFX/data/settings.conf" || { echo "FAIL $size: R twice in Display & sound didn't reach Clock"; fail=1; }
sed -i 's/^clock = 12h/clock = 24h/' "$UXFX/data/settings.conf"

# no noise: the home bubble has no second line repeating the name ("Nintendo DS" / "Nintendo"), and no count under a
# shelf of a few systems (the rail runs on under where "01 / 07" was)
ux_run noise "home 0; wait 2400; shot ux-noise"
ux_lt "$(ux_std "$OUT/$size-ux-noise-bot.png" 200 18 220 124)" 0.03 || { echo "FAIL $size: a second line under Nintendo DS"; fail=1; }
ux_lt "$(ux_std "$OUT/$size-ux-noise-bot.png" 50 3 566 400)" 0.12 || { echo "FAIL $size: a count beside the home rail"; fail=1; }

# Settings said on the home screen (START), and Power from the groups (Y): Sleep asks bananad with its file (and
# closes the settings); Shut down needs A twice
ux_run settings-hint "home 0; wait 2400; shot ux-home-hints"
ux_lt 0.02 "$(ux_std "$OUT/$size-ux-home-hints-bot.png" 120 30 250 428)" || { echo "FAIL $size: no START hint on the home screen"; fail=1; }
rm -f "$OUT/sleep-now"
ux_run power "home 0; press start; press y; shot ux-power; press a" BANANA_SLEEP_REQUEST="$OUT/sleep-now"
ux_check power "press a: settings closed"
[ -s "$OUT/sleep-now" ] || { echo "FAIL $size: Sleep didn't ask bananad"; fail=1; }
ux_run poweroff "home 0; press start; press y; press down; press down; press a; press a"
[ "$(grep -c 'press a: settings result' "$OUT/$size-poweroff.log")" = 2 ] && grep -q 'press a: settings result 6' "$OUT/$size-poweroff.log" \
    || { echo "FAIL $size: Shut down: $(grep 'press a' "$OUT/$size-poweroff.log" | tr '\n' ' ')"; fail=1; }

# Delete game (X on a game, the page's last row): A asks, A again deletes. The game only: its save stays. With its
# saves, states and art (Right): those go, and nothing else (another game's save, a BIOS of the same name, the other
# DS games); the library is read again without it
mkdir -p "$UXFX/saves/nds" "$UXFX/states/nds" "$UXFX/drastic/backup" "$UXFX/bios"
for f in "saves/nds/Peel Quest.sav" "states/nds/Peel Quest.ml1" "drastic/backup/Peel Quest.dsv" "saves/nds/Peel Quest 2.sav" "bios/Peel Quest.bin" "saves/nds/Split Racers (USA).sav"; do echo x > "$UXFX/$f"; done
D7="press down; press down; press down; press down; press down; press down; press down; press down;"  # (8 rows: + 3D resolution)
ux_run delete-keep "lib 0 3; press x; $D7 press a; press b; press b; lib 0 3; press x; $D7 press a; shot ux-delete-ask; press a; wait 300; shot ux-delete-done"
[ ! -e "$UXFX/roms/nds/Split Racers (USA).nds" ] && [ -e "$UXFX/saves/nds/Split Racers (USA).sav" ] && [ -e "$UXFX/roms/nds/media/box/Split Racers (USA).png" ] \
    || { echo "FAIL $size: Delete game (the game only): $(ls "$UXFX/roms/nds" "$UXFX/saves/nds" | tr '\n' ' ')"; fail=1; }
[ "$(grep -c '^banana-ui: deleted ' "$OUT/$size-delete-keep.log")" = 1 ] || { echo "FAIL $size: the first A deleted, or more than the ROM went"; fail=1; }
ux_check delete-keep "deleted: nds's list again, 4 games"
ux_run delete-all "lib 0 2; press x; $D7 press right; press a; press a; wait 300"
for f in "roms/nds/Peel Quest.nds" "saves/nds/Peel Quest.sav" "states/nds/Peel Quest.ml1" "drastic/backup/Peel Quest.dsv" "roms/nds/media/box/Peel Quest.png"; do
    [ -e "$UXFX/$f" ] && { echo "FAIL $size: Delete game left $f"; fail=1; }
done
for f in "saves/nds/Peel Quest 2.sav" "bios/Peel Quest.bin" "roms/nds/Banana Kart DS.nds" "roms/nds/media/box/Banana Kart DS.png"; do
    [ -e "$UXFX/$f" ] || { echo "FAIL $size: Delete game took $f"; fail=1; }
done

# the home row: a system with nothing to start is left off, Ports with PortMaster's own entry alone is on it (PortMaster
# gets ports: deleting the last port must not take it away); Display & sound > Shown systems (R twice, down) hides one
# (Game Boy Advance), then Ports (the last row)
UXP=$UXFX/roms/ports; mkdir -p "$UXP"; printf '#!/bin/bash\n' > "$UXP/PortMaster.sh"
ux_run shelves "home 0; press left; press start; press a; press r; press r; press down; press a; press down; press right; press up; press up; press right; press b; press start; home 0; press left"
grep -q '^shelf.gba = hide' "$UXFX/data/settings.conf" && grep -q '^shelf.ports = hide' "$UXFX/data/settings.conf" || { echo "FAIL $size: shown systems: $(grep shelf "$UXFX/data/settings.conf" | tr '\n' ' ')"; fail=1; }
# the row's systems: n at first with Ports (left from the first is n - 1), n - 1 without the GBA, n - 2 without Ports
first=$(sed -n 's/.*press left: used, view 0 sys \([0-9]*\).*/\1/p' "$OUT/$size-shelves.log" | head -1)
[ "$(grep -o 'shown systems: [0-9]*' "$OUT/$size-shelves.log" | cut -d' ' -f3 | tr '\n' ' ')" = "$first $((first - 1)) " ] \
    || { echo "FAIL $size: the home row after Shown systems (left at first: $first): $(grep 'shown systems' "$OUT/$size-shelves.log" | tr '\n' ' ')"; fail=1; }
sed -i '/^shelf\./d' "$UXFX/data/settings.conf"

# System > Language (its first row) asks before switching, No first: right then A keeps English; right, down, A
# switches (the menu starts again: settings result 9, LANGUAGE)
ux_run langask "home 0; press start; press r; press r; press r; press r; press r; press a; press right; shot ux-langask; press a"
grep -q '^language = zh-cn' "$UXFX/data/settings.conf" && { echo "FAIL $size: the language switched without a Yes"; fail=1; }
ux_check langask "press a: settings result 1"
ux_run langyes "home 0; press start; press r; press r; press r; press r; press r; press a; press right; press down; press a"
grep -q '^language = zh-cn' "$UXFX/data/settings.conf" || { echo "FAIL $size: Yes didn't switch the language"; fail=1; }
sed -i '/^language = /d' "$UXFX/data/settings.conf"

# System > Credits (after About: up four from the first row) opens the credits from credits.txt; with no names in it,
# no row (up four is then Restart, an action: A only arms it)
ux_run credits "home 0; press start; press r; press r; press r; press r; press r; press a; press up; press up; press up; press up; shot ux-credits-row; press a; shot ux-credits; press down; press b" BANANA_CREDITS="$HERE/../system/rootfs/etc/banana/credits.txt"
ux_lt 1000 "$(ux_diff "$OUT/$size-ux-credits-bot.png" "$OUT/$size-ux-credits-row-bot.png")" || { echo "FAIL $size: Credits didn't open"; fail=1; }
grep -q 'Majirefy = ' "$HERE/../system/rootfs/etc/banana/credits.txt" || { echo "FAIL: credits.txt lost Majirefy's credit"; fail=1; }

# System > Hotkeys (its last row: up from the first) opens the list of hotkeys, B back to System on its row
ux_run hotkeys "home 0; press start; press r; press r; press r; press r; press r; press a; press up; press a; wait 400; shot ux-hotkeys; press b; wait 400; shot ux-hotkeys-back"
ux_lt 1000 "$(ux_diff "$OUT/$size-ux-hotkeys-bot.png" "$OUT/$size-ux-hotkeys-back-bot.png")" || { echo "FAIL $size: Hotkeys didn't open"; fail=1; }

# the wordmark's B (tools/make-logo.py's B_GLYPH): square corners on the left, so it doesn't read as a G; in the boot
# animation's mask (word.png) its top-left and bottom-left pixels are set
[ "$(convert "$HERE/assets/splash/word.png" -format '%[fx:p{0,0}.intensity>0.5 && p{0,6}.intensity>0.5]' info: 2>/dev/null)" = 1 ] || { echo "FAIL: the wordmark's B has rounded corners again"; fail=1; }
ux_run splash-b "splash ux-boot-word 700"

# Settings' help texts fit their box on the top panel (feedback 2026-10-25: the Shader row's was cut off): every row's,
# and a game page's, in English and in Simplified Chinese, with either DS emulator's rows and either handheld's texts
# (the RG DS Plus adds a note to the Shader rows); "helpcheck" lays each out as the panel does
HCFX=$OUT/ux-help-fixture-$size
rm -rf "$HCFX"; "$HERE/tests/fixture.sh" "$HCFX" >/dev/null
for hl in en zh-cn; do
    for he in superdrastic bananads; do
        for hb in rgds rgds-plus; do
            touch "$HCFX/data/settings.conf"; sed -i '/^language\b/d; /^nds_emulator\b/d' "$HCFX/data/settings.conf"
            printf 'language = %s\nnds_emulator = %s\n' "$hl" "$he" >> "$HCFX/data/settings.conf"
            hn=help-$hl-$he-$hb
            BANANA_BOARD=$hb "$HERE/banana-ui" --headless "$OUT/$size-" --size $size --assets "$HERE/assets/rnds" --splash "$HERE/assets/splash" \
                --banana "$HERE/assets/banana" --systems "$HERE/../system/rootfs/etc/banana/systems.conf" --roms "$HCFX/roms" --data "$HCFX/data" \
                --script "home 0; helpcheck" 2> "$OUT/$size-$hn.log" || { echo "FAIL $size: banana-ui ($hn) exited $?"; fail=1; }
            [ "$hl" = en ] || ux_check $hn "language: .*(zh-cn)"
            hc=$(sed -n 's/.*helpcheck: \([0-9]*\) texts/\1/p' "$OUT/$size-$hn.log")
            [ "${hc:-0}" -ge 100 ] || { echo "FAIL $size ($hn): helpcheck checked ${hc:-no} texts"; fail=1; }
            grep 'helpcheck .*OVERFLOW' "$OUT/$size-$hn.log" | sed "s/^banana-ui: helpcheck /FAIL $size ($hn): help text too long: /" | grep . && fail=1
        done
    done
done

# the library's top screen (feedback 2026-10-25): a long title gets the lines it needs (not two), and the line under it,
# the stats and the achievements stay put whatever the title's length (the block was centred: it jumped while browsing)
LTFX=$OUT/ux-title-fixture-$size
rm -rf "$LTFX"; "$HERE/tests/fixture.sh" "$LTFX" >/dev/null
head -c 4096 /dev/zero > "$LTFX/roms/nds/Pokemon Mystery Dungeon - Explorers of Darkness.nds"    # three lines
UXFX_SAVE=$UXFX; UXFX=$LTFX
ux_run title "lib 0 1; wait 2400; shot ux-title-one; press right; press right; wait 2400; shot ux-title-three"
UXFX=$UXFX_SAVE
ux_check title "press right: used, view 1 sys 0 game 3"
# the "Nintendo DS" line and the stats under it (x 300-596, y 226-330): the same pixels for a one-line title and a
# three-line one (both games never played: the same values)
ux_crop() { convert "$1" -crop "$(awk -v k=$ux_k 'BEGIN { printf "%dx%d+%d+%d", 296 * k, 104 * k, 300 * k, 226 * k }')" +repage "$2"; }
ux_crop "$OUT/$size-ux-title-one-top.png" "$OUT/$size-ux-title-one-stats.png"
ux_crop "$OUT/$size-ux-title-three-top.png" "$OUT/$size-ux-title-three-stats.png"
[ "$(ux_diff "$OUT/$size-ux-title-one-stats.png" "$OUT/$size-ux-title-three-stats.png")" = 0 ] || { echo "FAIL $size: the game's details moved with the title's length"; fail=1; }
# the third title line is drawn (x 300-596, y 128-158: text there, not the sheet alone)
ux_lt 0.05 "$(ux_std "$OUT/$size-ux-title-three-top.png" 296 26 300 130)" || { echo "FAIL $size: a long title cut at two lines"; fail=1; }

# a translation longer than the English (a translator: "some labels in Russian end up longer"): a made-up language
# 1.6x as long in Cyrillic (pseudo-lang.py, from zh-cn.tsv's English). A label too long for its box scrolls while picked
# (the home cart's START, the Settings tile picked), and is cut with "..." of the TRANSLATION otherwise; in English, where
# everything fits, nothing scrolls and the menu sleeps (no wake for a marquee)
PSA=$OUT/ux-pseudo-assets-$size
rm -rf "$PSA"; mkdir -p "$PSA/rnds" "$PSA/lang"
for f in "$HERE"/assets/rnds/*; do ln -s "$f" "$PSA/rnds/"; done
python3 -I "$HERE/tests/pseudo-lang.py" "$HERE/assets/lang/zh-cn.tsv" "$PSA/lang/xx.tsv"
PSFX=$OUT/ux-pseudo-fixture-$size
rm -rf "$PSFX"; "$HERE/tests/fixture.sh" "$PSFX" >/dev/null; echo "language = xx" >> "$PSFX/data/settings.conf"
"$HERE/banana-ui" --headless "$OUT/$size-" --size $size --assets "$PSA/rnds" --splash "$HERE/assets/splash" --banana "$HERE/assets/banana" \
    --systems "$HERE/../system/rootfs/etc/banana/systems.conf" --roms "$PSFX/roms" --data "$PSFX/data" \
    --script "home 0; wait 2400; shot ux-mq-0; wait 1700; shot ux-mq-1; press start; shot ux-mq-g0; sleep 2600; shot ux-mq-g1" 2> "$OUT/$size-mq.log" \
    || { echo "FAIL $size: banana-ui (marquee) exited $?"; fail=1; }
ux_check mq "language: Псевдо (xx)"
ux_check mq "shot ux-mq-0 (frame [0-9.]* ms, wake 1[0-9][0-9][0-9])"     # the marquee's pause at its start: a wake, no frames
ux_crop2() { convert "$1" -crop "$(awk -v k=$ux_k -v w=$2 -v h=$3 -v x=$4 -v y=$5 'BEGIN { printf "%dx%d+%d+%d", w * k, h * k, x * k, y * k }')" +repage "$6"; }
ux_crop2 "$OUT/$size-ux-mq-0-bot.png" 108 28 266 310 "$OUT/$size-ux-mq-start0.png"
ux_crop2 "$OUT/$size-ux-mq-1-bot.png" 108 28 266 310 "$OUT/$size-ux-mq-start1.png"
[ "$(ux_diff "$OUT/$size-ux-mq-start0.png" "$OUT/$size-ux-mq-start1.png")" != 0 ] || { echo "FAIL $size: the cart's long START doesn't scroll"; fail=1; }
ux_crop2 "$OUT/$size-ux-mq-g0-bot.png" 190 24 20 154 "$OUT/$size-ux-mq-tile0.png"
ux_crop2 "$OUT/$size-ux-mq-g1-bot.png" 190 24 20 154 "$OUT/$size-ux-mq-tile1.png"
[ "$(ux_diff "$OUT/$size-ux-mq-tile0.png" "$OUT/$size-ux-mq-tile1.png")" != 0 ] || { echo "FAIL $size: the picked Settings tile's long name doesn't scroll"; fail=1; }
# English: nothing too long on the home screen, so no marquee wakes the menu
ux_run mq-en "home 0; wait 2400; shot ux-mq-en"
grep -q "shot ux-mq-en (frame [0-9.]* ms, wake -1)" "$OUT/$size-mq-en.log" || { echo "FAIL $size: the English home screen wakes for something: $(grep 'shot ux-mq-en' "$OUT/$size-mq-en.log")"; fail=1; }

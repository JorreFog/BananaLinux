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

# the home row goes round (Pixel, as in 0.4.1): Recently played starts it and the Apps tile ends it; left on the first
# goes to the last, and the last is drawn left of the first (and the first right of the last)
ux_run row "home recently-played; wait 2400; shot ux-row-first; press left; wait 2400; shot ux-row-last"
ux_check row "press left: used, view 0 sys 6"     # (Recently played, Favorites, four systems, Apps: Ports with PortMaster alone is off the row)
ux_lt "$(ux_std "$OUT/$size-ux-row-first-bot.png" 210 80 30 200)" 0.08 && { echo "FAIL $size: nothing drawn left of the first tile (the row doesn't go round)"; fail=1; }
ux_lt "$(ux_std "$OUT/$size-ux-row-last-bot.png" 210 80 400 200)" 0.08 && { echo "FAIL $size: nothing drawn right of the last tile (the row doesn't go round)"; fail=1; }
# the Apps tile, last: every app in its list (Bshare and Files here), none of them a tile of its own; A on one opens it
ux_run apps "homeid files; homeid bshare; shot ux-apps; press a"
ux_check apps "homeid files: app [0-9]* on the Apps tile 6"
ux_check apps "homeid bshare: app [0-9]* on the Apps tile 6"
ux_check apps "press a: banana:bshare open"
# the plate's START on its accent, the dark band below it (#128: the band cut through the letters): the plate under
# START (y 328) the same colour as above it (y 300)
ux_mean() { convert "$1" -crop "$(awk -v k=$ux_k -v y=$2 'BEGIN { printf "%dx%d+%d+%d", 100 * k, 4 * k, 270 * k, y * k }')" -format '%[fx:int(255*mean.r)] %[fx:int(255*mean.g)] %[fx:int(255*mean.b)]' info: 2>/dev/null; }
[ "$(ux_mean "$OUT/$size-ux-row-first-bot.png" 328)" = "$(ux_mean "$OUT/$size-ux-row-first-bot.png" 300)" ] || { echo "FAIL $size: the plate's dark band runs into START"; fail=1; }

# the box art's frame (Pixel): it fits the picture (owner, 0.5.0: a square frame stood taller than a DS box): a tall
# cover's frame top is elsewhere than a wide one's, and a game without one keeps the square
convert -size 260x420 "gradient:#3d9e9a-black" "$UXFX/roms/nds/media/box/Peel Quest.png"
ux_run cover "lib nds 0; wait 2400; shot ux-cover-wide; press right; wait 2400; shot ux-cover-none; press right; wait 2400; shot ux-cover-tall"
ux_strip() { convert "$1" -crop "$(awk -v k=$ux_k -v y=$2 -v h=$3 'BEGIN { printf "%dx%d+%d+%d", 250 * k, h * k, 38 * k, y * k }')" +repage "$4"; }
for s in wide none tall; do
    ux_strip "$OUT/$size-ux-cover-$s-top.png" 146 14 "$OUT/$size-ux-cover-$s-edge.png"	# (to y 160: the picture's own top edge, no border round it since 0.5.1)
    ux_strip "$OUT/$size-ux-cover-$s-top.png" 380 14 "$OUT/$size-ux-cover-$s-shadow.png"
done
[ "$(ux_diff "$OUT/$size-ux-cover-wide-edge.png" "$OUT/$size-ux-cover-tall-edge.png")" = 0 ] && { echo "FAIL $size: the frame didn't follow the cover's shape"; fail=1; }

# Last played with DS cartridges: Game icon (#107): the chip's cartridge shows the ROM's own icon (Yellow Planet's
# yellow disc) as the reel does, not its box art (blue here); with the default, the box as before
RIFX=$OUT/ux-resume-fixture-$size
rm -rf "$RIFX"; "$HERE/tests/fixture.sh" "$RIFX" >/dev/null
convert -size 420x380 "gradient:#4060d0-black" "$RIFX/roms/nds/media/box/Yellow Planet [!].png"
printf 'nds/Yellow Planet [!].zip\t2\t600\t%s\n' "$(date +%s)" >> "$RIFX/data/playstats.tsv"
UXFX_SAVE=$UXFX; UXFX=$RIFX
ux_run resume-box "home nds; wait 2400; shot ux-resume-box"
echo "ds_cart_art = icon" >> "$RIFX/data/settings.conf"
ux_run resume-icon "home nds; wait 2400; shot ux-resume-icon"
UXFX=$UXFX_SAVE
ux_yellow() { convert "$1" -crop "$(awk -v k=$ux_k 'BEGIN { printf "%dx%d+%d+%d", 30 * k, 30 * k, 233 * k, 333 * k }')" -format '%[fx:mean.r > 0.8 && mean.b < 0.3]' info: 2>/dev/null; }
[ "$(ux_yellow "$OUT/$size-ux-resume-icon-top.png")" = 1 ] || { echo "FAIL $size: Last played doesn't show the DS game's icon (Game icon)"; fail=1; }
[ "$(ux_yellow "$OUT/$size-ux-resume-box-top.png")" = 0 ] || { echo "FAIL $size: Last played shows the icon over the box art (default)"; fail=1; }

# a cartridge's label art is all on the label, fitted, on the plain label where it leaves room (#108: it was cut to
# the window): a title screen (256x192, a green strip down its left side) keeps its strip, the label shows above it
mkdir -p "$RIFX/roms/nds/media/label"
convert -size 256x192 xc:'#203080' -fill '#30c040' -draw 'rectangle 0,0 31,191' "$RIFX/roms/nds/media/label/Banana Kart DS.png"
sed -i '/^ds_cart_art = /d' "$RIFX/data/settings.conf"	# (the label art, not the icons)
UXFX=$RIFX
ux_run label "lib nds 0; wait 2400; shot ux-label"
UXFX=$UXFX_SAVE
ux_rgb() { convert "$1" -crop "$(awk -v k=$ux_k -v w=$2 -v h=$3 -v x=$4 -v y=$5 'BEGIN { printf "%dx%d+%d+%d", w * k, h * k, x * k, y * k }')" -format "%[fx:$6]" info: 2>/dev/null; }
[ "$(ux_rgb "$OUT/$size-ux-label-bot.png" 4 30 281 230 'mean.g > 0.6 && mean.r < 0.3')" = 1 ] || { echo "FAIL $size: the label art cut at its sides"; fail=1; }
[ "$(ux_rgb "$OUT/$size-ux-label-bot.png" 40 3 300 206 'mean > 0.85')" = 1 ] || { echo "FAIL $size: no plain label above a wide label art"; fail=1; }

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
ux_run ink-light "inkon f0b429; home nds; wait 2400; shot ux-ink-light"
ux_check ink-light "inkon f0b429: ink ffffff"
sed -i 's/^accent_text = .*//' "$UXFX/data/settings.conf"

# L/R go a page: five games in a list (the shoulders name the games they land on), five rows in a settings list
# (Look & sound, the third tile: its Clock row, Right sets 12-hour)
ux_run page "lib nds 0; wait 2400; shot ux-page; press r; press l; press start; row Look & sound clock; press right; press b; press b"
ux_check page "press r: used, view 1 sys [0-9]* game 4"
ux_check page "press l: used, view 1 sys [0-9]* game 0"
grep -q '^clock = 12h' "$UXFX/data/settings.conf" || { echo "FAIL $size: Look & sound > Clock didn't set 12-hour"; fail=1; }
sed -i 's/^clock = 12h/clock = 24h/' "$UXFX/data/settings.conf"

# Look & sound's pages: Date (Right: day/month), Sound > Menu sounds (Right: Quiet), Theme options > Reset theme
# options (A twice: the accent and motion back to the template's)
sed -i 's/^accent = .*//; s/^motion = .*//' "$UXFX/data/settings.conf"; printf 'accent = blue\nmotion = off\n' >> "$UXFX/data/settings.conf"
ux_run looksound "home nds; press start; row Look & sound date_format; press right; row Sound sounds; press right; shot ux-sound; row Theme options theme_reset; shot ux-themeopts; press a; press a; press b; press b"
grep -q '^date_format = dm$' "$UXFX/data/settings.conf" || { echo "FAIL $size: Look & sound > Date: Right didn't set day/month"; fail=1; }
grep -q '^sounds = quiet$' "$UXFX/data/settings.conf" || { echo "FAIL $size: Sound > Menu sounds: Right didn't set Quiet"; fail=1; }
grep -q '^accent = yellow$' "$UXFX/data/settings.conf" && grep -q '^motion = on$' "$UXFX/data/settings.conf" || { echo "FAIL $size: Reset theme options didn't put the accent and motion back: $(grep -E '^(accent|motion) =' "$UXFX/data/settings.conf" | tr '\n' ' ')"; fail=1; }
sed -i 's/^date_format = .*/date_format = md/; s/^sounds = .*/sounds = on/' "$UXFX/data/settings.conf"

# no noise: the home bubble has no second line repeating the name ("Nintendo DS" / "Nintendo"), and no count under a
# shelf of a few systems (the rail runs on under where "01 / 07" was)
ux_run noise "home nds; wait 2400; shot ux-noise"
ux_lt "$(ux_std "$OUT/$size-ux-noise-bot.png" 200 18 220 124)" 0.03 || { echo "FAIL $size: a second line under Nintendo DS"; fail=1; }
ux_lt "$(ux_std "$OUT/$size-ux-noise-bot.png" 50 3 566 400)" 0.12 || { echo "FAIL $size: a count beside the home rail"; fail=1; }

# Settings said on the home screen (START), and Power from the groups (Y): Sleep asks bananad with its file (and
# closes the settings) after A twice, as Shut down does; an accidental touch on the row only arms it
ux_run settings-hint "home nds; wait 2400; shot ux-home-hints"
ux_lt 0.02 "$(ux_std "$OUT/$size-ux-home-hints-bot.png" 120 30 250 428)" || { echo "FAIL $size: no START hint on the home screen"; fail=1; }
rm -f "$OUT/sleep-now"
ux_run power-arm "home nds; press start; press y; press a" BANANA_SLEEP_REQUEST="$OUT/sleep-now"
ux_check power-arm "press a: settings result"
[ ! -s "$OUT/sleep-now" ] || { echo "FAIL $size: one press on Sleep slept the handheld"; fail=1; }
ux_run power "home nds; press start; press y; shot ux-power; press a; shot ux-power-armed; press a" BANANA_SLEEP_REQUEST="$OUT/sleep-now"
ux_check power "press a: settings closed"
[ -s "$OUT/sleep-now" ] || { echo "FAIL $size: Sleep didn't ask bananad"; fail=1; }
ux_run poweroff "home nds; press start; press y; press down; press down; press a; press a"
[ "$(grep -c 'press a: settings result' "$OUT/$size-poweroff.log")" = 2 ] && grep -q 'press a: settings result 6' "$OUT/$size-poweroff.log" \
    || { echo "FAIL $size: Shut down: $(grep 'press a' "$OUT/$size-poweroff.log" | tr '\n' ' ')"; fail=1; }

# two games whose names are the same once the brackets are off (#137: a game and its hack): each keeps its file's name;
# a game whose name is its own keeps the tidy one
echo x > "$UXFX/roms/gb/Pocket Peel [Hack v1.0].gb"
ux_run same-names "lib gb 0; press x; press b; lib gb 1; press x; press b; lib gba 0; press x"
ux_check same-names "game page title: Pocket Peel\$"
ux_check same-names "game page title: Pocket Peel \[Hack v1.0\]\$"
ux_check same-names "game page title: Orchard Tactics\$"
rm -f "$UXFX/roms/gb/Pocket Peel [Hack v1.0].gb"
echo x > "$UXFX/roms/gb/Pocket Peel (USA).gb"; mv "$UXFX/roms/gb/Pocket Peel.gb" "$UXFX/roms/gb/Pocket Peel (Europe).gb"
ux_run same-names2 "lib gb 0; press x; press b; lib gb 1; press x"
ux_check same-names2 "game page title: Pocket Peel (Europe)\$"
ux_check same-names2 "game page title: Pocket Peel (USA)\$"
rm -f "$UXFX/roms/gb/Pocket Peel (USA).gb"; mv "$UXFX/roms/gb/Pocket Peel (Europe).gb" "$UXFX/roms/gb/Pocket Peel.gb"
# a program that ends (#228): a game's place in its library again; an app (Files here) its place in the Apps tile's
# list (the apps live there now, not as tiles of their own)
ux_run app-back "lib storeapps 0; ended 0; lib gba 0; ended 1"
[ "$(grep -c 'ended: view 1 sys [0-9]*$' "$OUT/$size-app-back.log")" = 2 ] || { echo "FAIL $size: app-back: $(grep ended "$OUT/$size-app-back.log" | tr '\n' ' ')"; fail=1; }
# Delete game (X on a game, the page's last row): A asks, A again deletes. The game only: its save stays. With its
# saves, states and art (Right): those go, and nothing else (another game's save, a BIOS of the same name, the other
# DS games); the library is read again without it
mkdir -p "$UXFX/saves/nds" "$UXFX/states/nds" "$UXFX/drastic/backup" "$UXFX/bios"
for f in "saves/nds/Peel Quest.sav" "states/nds/Peel Quest.ml1" "drastic/backup/Peel Quest.dsv" "saves/nds/Peel Quest 2.sav" "bios/Peel Quest.bin" "saves/nds/Split Racers (USA).sav"; do echo x > "$UXFX/$f"; done
D7="press down; press down; press down; press down; press down; press down; press down; press down; press down; press down; press down;"  # (11 rows: + 3D resolution, + Timing, + DS CPU speed, + Achievements)
ux_run delete-keep "lib nds 3; press x; $D7 press a; press b; press b; lib nds 3; press x; $D7 press a; shot ux-delete-ask; press a; wait 300; shot ux-delete-done"
[ ! -e "$UXFX/roms/nds/Split Racers (USA).nds" ] && [ -e "$UXFX/saves/nds/Split Racers (USA).sav" ] && [ -e "$UXFX/roms/nds/media/box/Split Racers (USA).png" ] \
    || { echo "FAIL $size: Delete game (the game only): $(ls "$UXFX/roms/nds" "$UXFX/saves/nds" | tr '\n' ' ')"; fail=1; }
[ "$(grep -c '^banana-ui: deleted ' "$OUT/$size-delete-keep.log")" = 1 ] || { echo "FAIL $size: the first A deleted, or more than the ROM went"; fail=1; }
ux_check delete-keep "deleted: nds's list again, 4 games"
# (and not another system's game of the same name's: the Super Nintendo's "Peel Quest" keeps its save and state, in the
# folders every system shares; nor those of a game whose name goes on from there in another system's folder)
mkdir -p "$UXFX/saves/gba" "$UXFX/states"
for f in "roms/snes/Peel Quest.sfc" "saves/Peel Quest.srm" "states/Peel Quest.state1" "roms/gb/Peel Quest.v2.gb" "saves/Peel Quest.v2.srm"; do echo x > "$UXFX/$f"; done
ux_run delete-all "lib nds 2; press x; $D7 press right; press a; press a; wait 300"
for f in "roms/nds/Peel Quest.nds" "saves/nds/Peel Quest.sav" "states/nds/Peel Quest.ml1" "drastic/backup/Peel Quest.dsv" "roms/nds/media/box/Peel Quest.png"; do
    [ -e "$UXFX/$f" ] && { echo "FAIL $size: Delete game left $f"; fail=1; }
done
for f in "saves/nds/Peel Quest 2.sav" "bios/Peel Quest.bin" "roms/nds/Banana Kart DS.nds" "roms/nds/media/box/Banana Kart DS.png" \
    "roms/snes/Peel Quest.sfc" "saves/Peel Quest.srm" "states/Peel Quest.state1" "saves/Peel Quest.v2.srm"; do
    [ -e "$UXFX/$f" ] || { echo "FAIL $size: Delete game took $f"; fail=1; }
done
rm -f "$UXFX/roms/snes/Peel Quest.sfc" "$UXFX/saves/Peel Quest.srm" "$UXFX/states/Peel Quest.state1" "$UXFX/roms/gb/Peel Quest.v2.gb" "$UXFX/saves/Peel Quest.v2.srm"
# the other way round: a GBA game named as a DS game, deleted with its saves: its own save goes (saves/gba), the DS
# game's stay (DraStic's newest one in drastic/backup, BananaDS's in saves/nds)
for f in "roms/gba/Banana Kart DS.gba" "saves/gba/Banana Kart DS.srm" "drastic/backup/Banana Kart DS.dsv" "saves/nds/Banana Kart DS.sav"; do echo x > "$UXFX/$f"; done
ux_run delete-gba "lib gba 0; press x; press up; press right; press a; press a; wait 300"
for f in "roms/gba/Banana Kart DS.gba" "saves/gba/Banana Kart DS.srm"; do
    [ -e "$UXFX/$f" ] && { echo "FAIL $size: Delete game (a GBA game named as a DS game) left $f"; fail=1; }
done
for f in "roms/nds/Banana Kart DS.nds" "drastic/backup/Banana Kart DS.dsv" "saves/nds/Banana Kart DS.sav"; do
    [ -e "$UXFX/$f" ] || { echo "FAIL $size: Delete game (a GBA game named as a DS game) took the DS game's $f"; fail=1; }
done
rm -f "$UXFX/drastic/backup/Banana Kart DS.dsv" "$UXFX/saves/nds/Banana Kart DS.sav"
# a game with ".." or "bios" in its name has its Delete row too (only a folder of that name is kept from it)
echo x > "$UXFX/roms/gba/Biosphere.gba"; echo x > "$UXFX/roms/gba/Wait... What.gba"
ux_run delete-names "lib gba 0; press x; press up; press a; press a; wait 300; lib gba 2; press x; press up; press a; press a; wait 300"
for f in "roms/gba/Biosphere.gba" "roms/gba/Wait... What.gba"; do
    [ -e "$UXFX/$f" ] && { echo "FAIL $size: no Delete for $f"; fail=1; rm -f "$UXFX/$f"; }
done
[ -e "$UXFX/roms/gba/Orchard Tactics.gba" ] && [ -e "$UXFX/roms/gba/Tiny Fruit Advance.gba" ] || { echo "FAIL $size: Delete game (names) took another game"; fail=1; }
# Networks: a saved network with a password, and a nearer one of the same name with none. With both in reach the row is
# the one with the password (A joins it with the saved password); with only the open one, A doesn't join it and the
# saved password stays (it put an open network in its place, joined from then on wherever one of that name was)
for twin in both only; do
    UXD=$OUT/ux-wifi-twin-$twin-$size; rm -rf "$UXD"; cp -r "$UXFX/data" "$UXD"
    printf 'network={\n\tssid="Cafe Free"\n\tkey_mgmt=NONE\n\tpriority=2\n}\nnetwork={\n\tssid="Banana Home"\n\tpsk="my-own-password"\n\tpriority=1\n}\n' > "$UXD/wifi.conf"
    BANANA_WIFI="$HERE/tests/fake-wifi.sh" FAKE_WIFI_TWIN=$twin "$HERE/banana-ui" --headless "$OUT/$size-" --size $size --assets "$HERE/assets/rnds" \
        --splash "$HERE/assets/splash" --banana "$HERE/assets/banana" --systems "$HERE/../system/rootfs/etc/banana/systems.conf" --roms "$UXFX/roms" --data "$UXD" \
        --script "home nds; press start; press r; press r; press r; press a; press up; press a; sleep 300; shot ux-wifi-twin-$twin; press a; sleep 200; shot ux-wifi-twin-$twin-a" 2> "$OUT/$size-wifi-twin-$twin.log"
    grep -q 'psk="my-own-password"' "$UXD/wifi.conf" && ! grep -A2 'ssid="Banana Home"' "$UXD/wifi.conf" | grep -q 'key_mgmt=NONE' ||
        { echo "FAIL $size: Networks ($twin): an open network of a saved one's name took the saved password's place: $(tr -d '\t' < "$UXD/wifi.conf" | tr '\n' ' ')"; fail=1; }
done
[ "$(sed -n 's/^[[:space:]]*ssid=//p' "$OUT/ux-wifi-twin-both-$size/wifi.conf" | head -n 1)" = '"Banana Home"' ] ||
    { echo "FAIL $size: Networks (both): A didn't join the saved network (its block not moved first)"; fail=1; }
[ "$(sed -n 's/^[[:space:]]*ssid=//p' "$OUT/ux-wifi-twin-only-$size/wifi.conf" | head -n 1)" = '"Cafe Free"' ] ||
    { echo "FAIL $size: Networks (only the open one): wifi.conf was written"; fail=1; }
# Networks: a new scan puts the networks in another order (a signal dropped): the cursor stays on its network, and Y
# forgets that one, not the one that has its row now
printf 'network={\n\tssid="Bravo"\n\tpsk="bravo-password"\n}\nnetwork={\n\tssid="Charlie"\n\tpsk="charlie-password"\n}\n' > "$OUT/ux-wifi-move.conf"
rm -f "$OUT/ux-wifi-move.n"
( UXD=$OUT/ux-wifi-data-$size; rm -rf "$UXD"; cp -r "$UXFX/data" "$UXD"; cp "$OUT/ux-wifi-move.conf" "$UXD/wifi.conf"
  BANANA_WIFI="$HERE/tests/fake-wifi.sh" FAKE_WIFI_MOVE="$OUT/ux-wifi-move.n" "$HERE/banana-ui" --headless "$OUT/$size-" --size $size --assets "$HERE/assets/rnds" \
      --splash "$HERE/assets/splash" --banana "$HERE/assets/banana" --systems "$HERE/../system/rootfs/etc/banana/systems.conf" --roms "$UXFX/roms" --data "$UXD" \
      --script "home nds; press start; press r; press r; press r; press a; press up; press a; sleep 300; press down; sleep 300; shot ux-wifi-move1; wait 16000; sleep 300; shot ux-wifi-move2; press y" 2> "$OUT/$size-wifi-move.log"
  grep -q 'ssid="Charlie"' "$UXD/wifi.conf" && ! grep -q 'ssid="Bravo"' "$UXD/wifi.conf" ) ||
    { echo "FAIL $size: Networks: after a new scan Y forgot another network than the one selected ($(grep -c ssid= "$OUT/ux-wifi-data-$size/wifi.conf") left: $(sed -n 's/.*ssid="\(.*\)"/\1/p' "$OUT/ux-wifi-data-$size/wifi.conf" | tr '\n' ' '))"; fail=1; }

# a card without roms/ports (#78: a deleted PortMaster folder once kept the menu from starting): the menu starts and
# Ports is off the row; with Shown systems > Ports > Always its shelf is on it (one more tile), and opens empty; a
# roms/ports that is a broken link the same
NPFX=$OUT/ux-noports-fixture-$size
rm -rf "$NPFX"; "$HERE/tests/fixture.sh" "$NPFX" >/dev/null
UXFX_SAVE=$UXFX; UXFX=$NPFX
ux_run noports "home recently-played; wait 2400; shot ux-noports; press left"
ux_check noports "press left: used, view 0 sys 6"
echo "shelf.ports = show" >> "$NPFX/data/settings.conf"
ux_run noports-show "home recently-played; press left; press left; press a; wait 2400; shot ux-noports-show"
ux_check noports-show "press left: used, view 0 sys 7"
ux_check noports-show "press a: used, view 1 sys 6"
ln -s nowhere "$NPFX/roms/ports"
ux_run noports-link "home recently-played; press left; press left; press a; wait 2400; shot ux-noports-link"
ux_check noports-link "press a: used, view 1 sys 6"
UXFX=$UXFX_SAVE

# the home row: a system with nothing to start is left off, Ports with PortMaster's own entry alone is on it (PortMaster
# gets ports: deleting the last port must not take it away); Look & sound > Home row (its third row) > Shown systems hides one
# (Game Boy Advance), then Ports (the row before Files, the last)
UXP=$UXFX/roms/ports; mkdir -p "$UXP"; printf '#!/bin/bash\n' > "$UXP/PortMaster.sh"
ux_run shelves "home recently-played; press left; press start; row Home row shelves; press a; press down; press right; press up; press up; press up; press right; press b; press start; home recently-played; press left"
grep -q '^shelf.gba = hide' "$UXFX/data/settings.conf" && grep -q '^shelf.ports = hide' "$UXFX/data/settings.conf" || { echo "FAIL $size: shown systems: $(grep shelf "$UXFX/data/settings.conf" | tr '\n' ' ')"; fail=1; }
# the row's systems: n at first with Ports (left from the first is n - 1), n - 1 without the GBA, n - 2 without Ports
first=$(sed -n 's/.*press left: used, view 0 sys \([0-9]*\).*/\1/p' "$OUT/$size-shelves.log" | head -1)
[ "$(grep -o 'shown systems: [0-9]*' "$OUT/$size-shelves.log" | cut -d' ' -f3 | tr '\n' ' ')" = "$first $((first - 1)) " ] \
    || { echo "FAIL $size: the home row after Shown systems (left at first: $first): $(grep 'shown systems' "$OUT/$size-shelves.log" | tr '\n' ' ')"; fail=1; }
sed -i '/^shelf\./d' "$UXFX/data/settings.conf"

# Look & sound > Home row > Apps: With the systems puts each app on the row (more tiles), In an Apps tile back to one
ux_run appstile "home nds; press start; row Home row apps_tile; press right; row Home row apps_tile; press left; press b; press b"
grep -q '^apps_tile = on$' "$UXFX/data/settings.conf" || { echo "FAIL $size: Home row > Apps: back to the tile wasn't saved"; fail=1; }
set -- $(grep -o 'shown systems: [0-9]*' "$OUT/$size-appstile.log" | cut -d' ' -f3)
[ $# -eq 2 ] && [ "$1" -gt "$2" ] || { echo "FAIL $size: Home row > Apps: the apps on the row didn't add tiles: $*"; fail=1; }

# System > Language (its first row) opens the list of languages (2026-11-04: a list, not left/right); down, A on the
# languages after English (es, then fi) asks before switching, No first: A keeps English; Yes (down, A) switches (the menu
# starts again: settings result 9, LANGUAGE)
ux_run langask "home nds; press start; press r; press r; press r; press r; press r; press a; press right; shot ux-langlist; press down; press down; press a; shot ux-langask; press a"
grep -q '^language = ' "$UXFX/data/settings.conf" && { echo "FAIL $size: the language switched without a Yes"; fail=1; }
ux_check langask "press a: settings result 1"
ux_run langyes "home nds; press start; press r; press r; press r; press r; press r; press a; press right; press down; press down; press a; press down; press a"
grep -q '^language = fi' "$UXFX/data/settings.conf" || { echo "FAIL $size: Yes didn't switch the language"; fail=1; }
sed -i '/^language = /d' "$UXFX/data/settings.conf"

# System > Credits (after About) opens the credits from credits.txt; with no names in it, no row
ux_run credits "home nds; press start; row System credits; shot ux-credits-row; press a; shot ux-credits; press down; press b" BANANA_CREDITS="$HERE/../system/rootfs/etc/banana/credits.txt"
ux_lt 1000 "$(ux_diff "$OUT/$size-ux-credits-bot.png" "$OUT/$size-ux-credits-row-bot.png")" || { echo "FAIL $size: Credits didn't open"; fail=1; }
grep -q 'Majirefy = ' "$HERE/../system/rootfs/etc/banana/credits.txt" || { echo "FAIL: credits.txt lost Majirefy's credit"; fail=1; }

# System > Hotkeys opens the list of hotkeys, B back to System on its row
ux_run hotkeys "home nds; press start; row System hotkeys; press a; wait 400; shot ux-hotkeys; press b; wait 400; shot ux-hotkeys-back"
ux_lt 1000 "$(ux_diff "$OUT/$size-ux-hotkeys-bot.png" "$OUT/$size-ux-hotkeys-back-bot.png")" || { echo "FAIL $size: Hotkeys didn't open"; fail=1; }
# Nintendo DS > Emulator configurations > SuperDrastic (its first row) opens SuperDrastic's own page, and Hotkeys there
# (third from its end) its hotkeys; B back on that row, B again back on SuperDrastic in Emulator configurations
ux_run hotkeys-sd "home nds; press start; row Emulator configurations nds_emu_sd; wait 400; shot ux-ds-row; press a; press up; press up; press up; wait 400; shot ux-sd-page; press a; wait 400; shot ux-hotkeys-sd; press b; wait 400; shot ux-hotkeys-sd-back; press b; wait 400; shot ux-sd-back"
ux_lt 1000 "$(ux_diff "$OUT/$size-ux-ds-row-bot.png" "$OUT/$size-ux-sd-page-bot.png")" || { echo "FAIL $size: Emulator configurations > SuperDrastic didn't open"; fail=1; }
ux_lt 1000 "$(ux_diff "$OUT/$size-ux-hotkeys-sd-bot.png" "$OUT/$size-ux-hotkeys-sd-back-bot.png")" || { echo "FAIL $size: SuperDrastic > Hotkeys didn't open"; fail=1; }
# (the list's header, its title and "row / rows" (x 18-622, y 16-66), the same: the same page and row, wherever it scrolled)
ux_head() { convert "$1" -crop "$(awk -v k=$ux_k 'BEGIN { printf "%dx%d+%d+%d", 604 * k, 50 * k, 18 * k, 16 * k }')" +repage "$2"; }
for s in sd-page hotkeys-sd-back ds-row sd-back; do ux_head "$OUT/$size-ux-$s-bot.png" "$OUT/$size-ux-$s-head.png"; done
[ "$(ux_diff "$OUT/$size-ux-sd-page-head.png" "$OUT/$size-ux-hotkeys-sd-back-head.png")" = 0 ] || { echo "FAIL $size: B from SuperDrastic's hotkeys wasn't back on its Hotkeys row"; fail=1; }
[ "$(ux_diff "$OUT/$size-ux-ds-row-head.png" "$OUT/$size-ux-sd-back-head.png")" = 0 ] || { echo "FAIL $size: B from SuperDrastic wasn't back on its row in Emulator configurations"; fail=1; }
# X on a GBA game: its system page's rows for this game only, each "Same as system" first, under Achievements. Screen is the first (no core
# installed here, so no Emulator row): Right twice keeps "bottom" for this game in banana/ra-per-game-screen.conf,
# as "gba/<ROM file name> = bottom" (what run-retroarch's pg and the menu's game start read)
ux_run pg-ra "homeid gba; wait 2400; press a; wait 2400; press x; press down; press right; press right"
grep -q '^gba/[^/]*\.gba = bottom$' "$UXFX/data/ra-per-game-screen.conf" 2>/dev/null || { echo "FAIL $size: a GBA game's own Screen wasn't kept: $(cat "$UXFX/data/ra-per-game-screen.conf" 2>/dev/null)"; fail=1; }
rm -f "$UXFX/data/ra-per-game-screen.conf"
# Other systems > Picture & sound opens its page (Shader first), B back on its row
ux_run ra-picture "home nds; press start; row Other systems ra_picture; wait 400; shot ux-ra-row; press a; wait 400; shot ux-ra-picture; press b; wait 400; shot ux-ra-back"
for s in ra-row ra-picture ra-back; do ux_head "$OUT/$size-ux-$s-bot.png" "$OUT/$size-ux-$s-head.png"; done
ux_lt 1000 "$(ux_diff "$OUT/$size-ux-ra-row-bot.png" "$OUT/$size-ux-ra-picture-bot.png")" || { echo "FAIL $size: Other systems > Picture & sound didn't open"; fail=1; }
[ "$(ux_diff "$OUT/$size-ux-ra-row-head.png" "$OUT/$size-ux-ra-back-head.png")" = 0 ] || { echo "FAIL $size: B from Picture & sound wasn't back on its row in Other systems"; fail=1; }

# the wordmark's B (tools/make-logo.py's B_GLYPH): square corners on the left, so it doesn't read as a G; in the boot
# animation's mask (word.png) its top-left and bottom-left pixels are set
[ "$(convert "$HERE/assets/splash/word.png" -format '%[fx:p{0,0}.intensity>0.5 && p{0,6}.intensity>0.5]' info: 2>/dev/null)" = 1 ] || { echo "FAIL: the wordmark's B has rounded corners again"; fail=1; }
ux_run splash-b "splash ux-boot-word 700"

# Settings' help texts fit their box on the top panel (feedback 2026-10-25: the Shader row's was cut off): every row's,
# and a game page's, in English and in Simplified Chinese, with either DS emulator's rows and either handheld's texts
# (the RG DS Plus adds a note to the Shader rows); "helpcheck" lays each out as the panel does
HCFX=$OUT/ux-help-fixture-$size
rm -rf "$HCFX"; "$HERE/tests/fixture.sh" "$HCFX" >/dev/null
for hl in en zh-cn ru it fi; do
    for he in superdrastic bananads; do
        for hb in rgds rgds-plus; do
            touch "$HCFX/data/settings.conf"; sed -i '/^language\b/d; /^nds_emulator\b/d' "$HCFX/data/settings.conf"
            printf 'language = %s\nnds_emulator = %s\n' "$hl" "$he" >> "$HCFX/data/settings.conf"
            hn=help-$hl-$he-$hb
            BANANA_BOARD=$hb "$HERE/banana-ui" --headless "$OUT/$size-" --size $size --assets "$HERE/assets/rnds" --splash "$HERE/assets/splash" \
                --banana "$HERE/assets/banana" --systems "$HERE/../system/rootfs/etc/banana/systems.conf" --roms "$HCFX/roms" --data "$HCFX/data" \
                --script "home nds; helpcheck" 2> "$OUT/$size-$hn.log" || { echo "FAIL $size: banana-ui ($hn) exited $?"; fail=1; }
            [ "$hl" = en ] || ux_check $hn "language: .*($hl)"
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
ux_run title "lib nds 1; wait 2400; shot ux-title-one; press right; press right; wait 2400; shot ux-title-three"
UXFX=$UXFX_SAVE
ux_check title "press right: used, view 1 sys 2 game 3"
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
    --script "home nds; wait 2400; shot ux-mq-0; wait 1700; shot ux-mq-1; press start; shot ux-mq-g0; sleep 2600; shot ux-mq-g1" 2> "$OUT/$size-mq.log" \
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
ux_run mq-en "home nds; wait 2400; shot ux-mq-en"
grep -q "shot ux-mq-en (frame [0-9.]* ms, wake -1)" "$OUT/$size-mq-en.log" || { echo "FAIL $size: the English home screen wakes for something: $(grep 'shot ux-mq-en' "$OUT/$size-mq-en.log")"; fail=1; }

# Nintendo DS > Emulator configurations > BananaDS opens BananaDS's own page (its header isn't SuperDrastic's)
ux_run emu-bds "home nds; press start; row Emulator configurations nds_emu_bds; press a; wait 400; shot ux-emu-bds"
ux_head "$OUT/$size-ux-emu-bds-bot.png" "$OUT/$size-ux-emu-bds-head.png"
[ "$(ux_diff "$OUT/$size-ux-emu-bds-head.png" "$OUT/$size-ux-sd-page-head.png")" = 0 ] && { echo "FAIL $size: Emulator configurations > BananaDS opened SuperDrastic's page"; fail=1; }
sed -i '/^nds_emulator = /d' "$UXFX/data/settings.conf"

# Glass while scrolling through many games: the wallpaper stays. The theme's picture cache dropped the first made
# (the wallpaper) once full, not the least recently drawn, and the screens went plain black (white in Glass light)
# until it was made again: 200 games with box art, a frame after each step right (snap: as the device draws it), and
# the top screen's corner (wallpaper only) as in the first shot in every one
GLFX=$OUT/ux-glass-fixture-$size
rm -rf "$GLFX"; "$HERE/tests/fixture.sh" "$GLFX" >/dev/null
mkdir -p "$GLFX/roms/gba/media/box"
convert -size 300x200 "gradient:#c04080-#203060" "$OUT/glass-box.png"
for i in $(seq 100 299); do : > "$GLFX/roms/gba/Glass $i.gba"; cp "$OUT/glass-box.png" "$GLFX/roms/gba/media/box/Glass $i.png"; done
rm -f "$OUT/$size"-ux-glass-*
gl_script="lib gba 0; wait 2400; shot ux-glass-first"
for i in $(seq 200); do gl_script="$gl_script; press right; wait 40; snap ux-glass-$i"; done
"$HERE/banana-ui" --headless "$OUT/$size-" --size $size --assets "$HERE/assets/rnds" --banana "$HERE/assets/banana" \
    --systems "$HERE/../system/rootfs/etc/banana/systems.conf" --roms "$GLFX/roms" --data "$GLFX/data" \
    --theme "$HERE/../system/rootfs/usr/share/banana/themes/glass-dark.json" \
    --script "$gl_script" 2> "$OUT/$size-glass.log" || { echo "FAIL $size: banana-ui (glass) exited $?"; fail=1; }
gl_corner() { convert "$1" -crop "$(awk -v k=$ux_k 'BEGIN { printf "%dx%d+%d+%d", 60 * k, 30 * k, 0, 440 * k }')" -format '%[fx:int(255*mean)]' info: 2>/dev/null; }
a=$(gl_corner "$OUT/$size-ux-glass-first-top.png"); gl_bad=0
for i in $(seq 200); do [ "$(gl_corner "$OUT/$size-ux-glass-$i-top.png")" = "$a" ] || gl_bad=$((gl_bad + 1)); done
[ -n "$a" ] && [ $gl_bad = 0 ] || { echo "FAIL $size: Glass's wallpaper gone in $gl_bad of 200 frames scrolling"; fail=1; }
rm -f "$OUT/$size"-ux-glass-[0-9]*

# Plaza: the games in pages side by side (5 x 3 at its start). Right at a row's end goes on to the next page's same
# row, a swipe turns the page, up stays on the page; R and L zoom (kept per system in zoom.txt); the A-Z button
# (tapped) goes to the next letter's first game; a tapped X is the host's (the game's page)
PZFX=$OUT/ux-plaza-fixture-$size
rm -rf "$PZFX"; "$HERE/tests/fixture.sh" "$PZFX" >/dev/null
rm -f "$PZFX"/roms/gba/*.gba
for i in $(seq 10 29); do : > "$PZFX/roms/gba/Apple $i.gba"; done
for i in $(seq 10 19); do : > "$PZFX/roms/gba/Berry $i.gba"; done
"$HERE/banana-ui" --headless "$OUT/$size-" --size $size --assets "$HERE/assets/rnds" --banana "$HERE/assets/banana" \
    --systems "$HERE/../system/rootfs/etc/banana/systems.conf" --roms "$PZFX/roms" --data "$PZFX/data" \
    --theme "$HERE/../system/rootfs/usr/share/banana/themes/plaza-light.json" \
    --script "lib gba 0; wait 1500; shot ux-plaza; press right; press right; press right; press right; press right; press up; swipe 100 240 500 240; swipe 500 240 100 240; press down; press r; press r; press l; touch 1 560 31; touch 1 300 450" \
    2> "$OUT/$size-plaza.log" || { echo "FAIL $size: banana-ui (plaza) exited $?"; fail=1; }
ux_check plaza "press right: used, view 1 sys [0-9]* game 4"
ux_check plaza "press right: used, view 1 sys [0-9]* game 15"          # the next page, the same (first) row
ux_check plaza "press up: unused, view 1 sys [0-9]* game 15"           # (the page's top row)
ux_check plaza "swipe 100 240 -> 500 240: used"                         # back a page: 0, the same place
ux_check plaza "swipe 500 240 -> 100 240: used"                         # on again: 15
ux_check plaza "press down: used, view 1 sys [0-9]* game 20"
ux_check plaza "press r: used"
ux_check plaza "press r: unused"                                       # (the biggest already)
grep -q "gba 1" "$PZFX/data/zoom.txt" 2>/dev/null || { echo "FAIL $size: Plaza's zoom isn't kept for the system ($(cat "$PZFX/data/zoom.txt" 2>/dev/null))"; fail=1; }
ux_check plaza "touch 560 31: used"                                    # A-Z: Berry 10, the first B
ux_check plaza "touch 300 450: used"
ux_check plaza "touch: key x"
if grep "theme:" "$OUT/$size-plaza.log"; then echo "FAIL $size: Plaza's theme has errors"; fail=1; fi

# Look & sound > Scrape artworks > Systems: a row each system artworks come for (Fetch or Skip); Right on the first sets
# it to Skip (settings.conf's art.<id> = off, which the fetch leaves out)
ux_run artsys "home nds; press start; row Scrape artworks art_systems; press a; shot ux-artsys; press right; press b; press b; press b"
grep -q '^art\.[a-z0-9-]* = off' "$UXFX/data/settings.conf" || { echo "FAIL $size: Scrape artworks > Systems: Skip wasn't saved: $(grep '^art\.' "$UXFX/data/settings.conf" | tr '\n' ' ')"; fail=1; }
sed -i '/^art\./d' "$UXFX/data/settings.conf"

# a PICO-8 cart (.p8.png) is its own picture: with no box art, the cart itself is the game's art (a red one here, on
# the top screen as its box)
P8FX=$OUT/ux-p8-fixture-$size
rm -rf "$P8FX"; "$HERE/tests/fixture.sh" "$P8FX" >/dev/null
mkdir -p "$P8FX/roms/pico-8"; convert -size 160x205 xc:'#e01020' "$P8FX/roms/pico-8/Red Cart.p8.png"
UXFX_SAVE=$UXFX; UXFX=$P8FX
ux_run p8 "lib pico-8 0; wait 2400; shot ux-p8"
UXFX=$UXFX_SAVE
p8_red=$(convert "$OUT/$size-ux-p8-top.png" -fx '(r > 0.8 && g < 0.2 && b < 0.25) ? 1 : 0' -format '%[fx:mean > 0.02]' info: 2>/dev/null)
[ "$p8_red" = 1 ] || { echo "FAIL $size: a PICO-8 cart's own picture isn't its art"; fail=1; }

# Glass: an app on the Apps tile is the tile it was on the home row (its own colour and icon), not a game's card:
# BShare (left of Files) a blue tile, where a game's card is dark. And PICO-8's Splore (an empty Splore.p8) has its own
# picture, a globe and a magnifier (the globe's blue on the top screen), in Glass and in the default theme
APFX=$OUT/ux-apps-fixture-$size
rm -rf "$APFX"; "$HERE/tests/fixture.sh" "$APFX" >/dev/null
mkdir -p "$APFX/roms/pico-8"; : > "$APFX/roms/pico-8/Splore.p8"
"$HERE/banana-ui" --headless "$OUT/$size-" --size $size --assets "$HERE/assets/rnds" --banana "$HERE/assets/banana" \
    --systems "$HERE/../system/rootfs/etc/banana/systems.conf" --roms "$APFX/roms" --data "$APFX/data" \
    --theme "$HERE/../system/rootfs/usr/share/banana/themes/glass-dark.json" \
    --script "homeid files; wait 2400; shot ux-glass-apps; lib pico-8 0; wait 2400; shot ux-glass-splore" 2> "$OUT/$size-glass-apps.log" ||
    { echo "FAIL $size: banana-ui (glass apps) exited $?"; fail=1; }
UXFX_SAVE=$UXFX; UXFX=$APFX
ux_run splore "lib pico-8 0; wait 2400; shot ux-splore"
UXFX=$UXFX_SAVE
ap_blue() { convert "$1" -crop "$(awk -v k=$ux_k -v x=$2 -v y=$3 'BEGIN { printf "%dx%d+%d+%d", 30 * k, 30 * k, x * k, y * k }')" -format '%[fx:mean.b > 0.6 && mean.b > mean.r + 0.25]' info: 2>/dev/null; }
[ "$(ap_blue "$OUT/$size-ux-glass-apps-bot.png" 140 194)" = 1 ] || { echo "FAIL $size: Glass's Apps: BShare isn't its blue tile"; fail=1; }
sp_blue() { convert "$1" -fx '(r > 0.1 && r < 0.25 && g > 0.6 && g < 0.75 && b > 0.95) ? 1 : 0' -format '%[fx:mean > 0.01]' info: 2>/dev/null; }
[ "$(sp_blue "$OUT/$size-ux-glass-splore-top.png")" = 1 ] || { echo "FAIL $size: Glass: Splore without its picture"; fail=1; }
[ "$(sp_blue "$OUT/$size-ux-splore-top.png")" = 1 ] || { echo "FAIL $size: Splore without its picture"; fail=1; }
# with PICO-8's own icon beside it (the owner's copy: lexaloffle-pico8.png, a magenta stand-in here) that icon is
# Splore's picture instead
convert -size 128x128 xc:'#ff00ff' "$APFX/roms/pico-8/lexaloffle-pico8.png"
UXFX_SAVE=$UXFX; UXFX=$APFX
ux_run splore-own "lib pico-8 0; wait 2400; shot ux-splore-own"
UXFX=$UXFX_SAVE
[ "$(convert "$OUT/$size-ux-splore-own-top.png" -fx '(r > 0.9 && g < 0.1 && b > 0.9) ? 1 : 0' -format '%[fx:mean > 0.02]' info: 2>/dev/null)" = 1 ] ||
    { echo "FAIL $size: Splore doesn't show PICO-8's own icon"; fail=1; }

# Glass: an app from the Store (an entry of the Apps system, as eReaderDS) is its own icon from the Store (a magenta
# stand-in here) on its tile, not the Apps tile's four squares
SAFX=$OUT/ux-storeapp-fixture-$size
rm -rf "$SAFX"; "$HERE/tests/fixture.sh" "$SAFX" >/dev/null
mkdir -p "$SAFX/tiles/media"; printf '#!/bin/sh\n' > "$SAFX/tiles/Reader.sh"
convert -size 128x128 xc:'#ff00ff' "$SAFX/tiles/media/reader.png"
printf '<?xml version="1.0"?>\n<gameList>\n  <game>\n    <path>./Reader.sh</path>\n    <name>Reader</name>\n    <image>./media/reader.png</image>\n  </game>\n</gameList>\n' > "$SAFX/tiles/gamelist.xml"
sed "s|^folder = /data/store/tiles|folder = $SAFX/tiles|" "$HERE/../system/rootfs/etc/banana/systems.conf" > "$SAFX/systems.conf"
"$HERE/banana-ui" --headless "$OUT/$size-" --size $size --assets "$HERE/assets/rnds" --banana "$HERE/assets/banana" \
    --systems "$SAFX/systems.conf" --roms "$SAFX/roms" --data "$SAFX/data" \
    --theme "$HERE/../system/rootfs/usr/share/banana/themes/glass-dark.json" \
    --script "lib storeapps 0; wait 2400; shot ux-glass-storeapp" 2> "$OUT/$size-glass-storeapp.log" ||
    { echo "FAIL $size: banana-ui (glass store app) exited $?"; fail=1; }
[ "$(convert "$OUT/$size-ux-glass-storeapp-bot.png" -fx '(r > 0.9 && g < 0.1 && b > 0.9) ? 1 : 0' -format '%[fx:mean > 0.003]' info: 2>/dev/null)" = 1 ] ||
    { echo "FAIL $size: Glass: a Store app's tile isn't its own icon"; fail=1; }


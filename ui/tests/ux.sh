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
ux_check row "press left: used, view 0 sys 6"
ux_lt "$(ux_std "$OUT/$size-ux-row-first-bot.png" 210 80 30 200)" 0.08 || { echo "FAIL $size: something drawn left of the first system"; fail=1; }
ux_lt "$(ux_std "$OUT/$size-ux-row-last-bot.png" 210 80 400 200)" 0.08 || { echo "FAIL $size: something drawn right of the last system"; fail=1; }

# the box art's frame (Pixel): one size for every game, the picture fitted in it; the frame's top edge and its shadow
# are the same pixels for a landscape cover, a tall one and a game with none (they moved with the picture's size)
convert -size 260x420 "gradient:#3d9e9a-black" "$UXFX/roms/nds/media/box/Peel Quest.png"
ux_run cover "lib 0 0; wait 2400; shot ux-cover-wide; press right; wait 2400; shot ux-cover-none; press right; wait 2400; shot ux-cover-tall"
ux_strip() { convert "$1" -crop "$(awk -v k=$ux_k -v y=$2 -v h=$3 'BEGIN { printf "%dx%d+%d+%d", 250 * k, h * k, 38 * k, y * k }')" +repage "$4"; }
for s in wide none tall; do
    ux_strip "$OUT/$size-ux-cover-$s-top.png" 146 9 "$OUT/$size-ux-cover-$s-edge.png"
    ux_strip "$OUT/$size-ux-cover-$s-top.png" 380 14 "$OUT/$size-ux-cover-$s-shadow.png"
done
for s in none tall; do
    for part in edge shadow; do
        [ "$(ux_diff "$OUT/$size-ux-cover-wide-$part.png" "$OUT/$size-ux-cover-$s-$part.png")" = 0 ] || { echo "FAIL $size: the cover's $part moved ($s)"; fail=1; }
    done
done

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

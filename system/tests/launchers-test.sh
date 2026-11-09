#!/bin/sh
# The DS launchers' file work against a made-up card, no emulator: run-drastic's per-game drastic.cfg lines (#117)
# and nds-buttons' DraStic controls (#151), taken from the launchers themselves (their functions, not a copy); nds-saves on
# a made-up data partition (BANANA_ROOT): an empty save never goes over a real one (#96).
#   sh system/tests/launchers-test.sh
cd "$(dirname "$0")/../.." || exit 1
R=$PWD L=$PWD/system/rootfs/usr/libexec/banana
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
passed=0 failed=0
check() { if [ "$1" = "$2" ]; then passed=$((passed + 1)); echo "  ok: $3"; else failed=$((failed + 1)); echo "  FAIL: $3 (got '$1', want '$2')"; fi; }
# fn <file> <name>...: those shell functions out of a launcher, defined here
fn() { f=$1; shift; for n; do eval "$(sed -n "/^$n() {/,/^}/p" "$f")"; done; }

echo "run-bananads: the dsi timing and Settings' DS CPU speed"
fn "$L/run-bananads" dsi_arm9
ARM9T=$(mktemp)
arm9() { (unset BANANADS_CORE_ARM9_MHZ; dsi_arm9 "$1"; echo "${BANANADS_CORE_ARM9_MHZ:-conf}"); }
check "$(arm9 /nonexistent)" "133" "no bananads.conf: the DSi's 133"
: > "$ARM9T"; check "$(arm9 "$ARM9T")" "133" "no line: 133"
echo "core.arm9_mhz = 67" > "$ARM9T"; check "$(arm9 "$ARM9T")" "133" "Settings' 67 under the game's dsi timing: 133"
echo "core.arm9_mhz = 133" > "$ARM9T"; check "$(arm9 "$ARM9T")" "conf" "Settings' 133: its own"
printf 'render.scale = 3\ncore.arm9_mhz = 1066\n' > "$ARM9T"; check "$(arm9 "$ARM9T")" "conf" "Settings' 1066 wins over dsi's 133"
fn "$L/run-bananads" own_arm9
own() { (export BANANADS_CORE_ARM9_MHZ=$1; own_arm9 "$2"; echo "$BANANADS_CORE_ARM9_MHZ"); }
check "$(own 133 1066)" "1066" "the game's own speed wins over its dsi timing's 133"
check "$(own 133 67)" "67" "... also when it is lower"
check "$(own 133 '')" "133" "no own speed: as it was"
rm -f "$ARM9T"

echo "run-drastic: a game's own 3D resolution (hires_3d) and Threaded 3D"
fn "$L/run-drastic" cfg_game cfg_back hires_of
check "$(hires_of 1) $(hires_of 2) $(hires_of 3) $(hires_of 4) [$(hires_of '')] [$(hires_of 5)]" "0 1 1 1 [] []" "1x is 0, 2x 3x 4x are 1, nothing else"
cd "$T" && mkdir config || exit 1
hires() { sed -n '/^hires_3d = /{s///p;q}' config/drastic.cfg; }
# run <hires_3d before> <game's res>: what run-drastic does around SuperDrastic; prints the line during the run, after, and whether it would exec
run() {
    printf 'threaded_3d = 0\nhires_3d = %s\n' "$1" > config/drastic.cfg
    CH=; cfg_game hires_3d "$(hires_of "$2")" && CH=1
    during=$(hires); marker=$( [ -f config/.hires_3d.settings ] && cat config/.hires_3d.settings)
    cfg_back hires_3d
    echo "${CH:-exec} during=$during marker=${marker:-none} after=$(hires) left=$(ls -A config | grep -c settings)"
}
check "$(run 0 '')" "exec during=0 marker=none after=0 left=0" "no line for the game: Settings' 0, exec"
check "$(run 1 1)" "1 during=0 marker=1 after=1 left=0" "1x over Settings' 2x: 0 for the run, 1 back"
check "$(run 0 1)" "exec during=0 marker=none after=0 left=0" "1x over Settings' 1x: no change, no marker"
check "$(run 0 2)" "1 during=1 marker=0 after=0 left=0" "2x over 1x: 1 for the run, 0 back"
check "$(run 0 4)" "1 during=1 marker=0 after=0 left=0" "4x is 2x (never hires_3d = 4)"
check "$(run 1 3)" "exec during=1 marker=none after=1 left=0" "3x over Settings' 2x: no change"
# both lines: Threaded 3D and the 3D resolution together, both back
printf 'threaded_3d = 1\nhires_3d = 0\n' > config/drastic.cfg
cfg_game threaded_3d 0; cfg_game hires_3d 1
check "$(tr '\n' ' ' < config/drastic.cfg)" "threaded_3d = 0 hires_3d = 1 " "both of the game's lines in for the run"
cfg_back threaded_3d; cfg_back hires_3d
check "$(tr '\n' ' ' < config/drastic.cfg)[$(ls -A config | grep -c settings)]" "threaded_3d = 1 hires_3d = 0 [0]" "both of Settings' back, no marker left"
# a power cut mid-run: the next start puts Settings' back from the marker; an empty one is dropped
printf 'threaded_3d = 0\nhires_3d = 0\n' > config/drastic.cfg; echo 1 > config/.hires_3d.settings
cfg_back hires_3d
check "$(hires) $(ls -A config | grep -c settings)" "1 0" "power cut: Settings' 1 back at the next start, marker gone"
: > config/.hires_3d.settings
cfg_back hires_3d
check "$(hires) $(ls -A config | grep -c settings)" "1 0" "an empty marker is dropped, the line left alone"
cfg_game hires_3d 4
check "$(hires) $(ls -A config | grep -c settings)" "1 0" "a value other than 0 or 1 never goes in"

echo "nds-buttons: DraStic's L2, R2, L3, R3 from Settings > Nintendo DS > Buttons (#151, replaces #113/#77's fixed move)"
IMG=$R/buildroot/board/rgds/drastic.cfg
keys() { grep -E "^controls_$1\[CONTROL_INDEX_(FAST_FORWARD|SAVE_STATE|LOAD_STATE|FAKE_MICROPHONE|MENU)\]" "${2:-config/drastic.cfg}" | sed 's/.*_INDEX_//; s/\] = /=/' | tr '\n' ' '; }
check "$(keys a "$IMG")" "MENU=65535 SAVE_STATE=1054 LOAD_STATE=1055 FAST_FORWARD=1031 FAKE_MICROPHONE=1035 " "the image's controls_a: F+R save, F+L load, R2 fast forward, L3 blow, L3 not the menu"
check "$(grep -c '^controls_a.* = 1036$' "$IMG")" "0" "nothing on R3 in the image's set a"
# a card from before (0.5.0.1: fast forward R3, save R2, load L2, the menu on L3) with the defaults: the image's set a
sed -e 's/^\(controls_a\[CONTROL_INDEX_SAVE_STATE\]\) = .*/\1 = 1031/' -e 's/^\(controls_a\[CONTROL_INDEX_LOAD_STATE\]\) = .*/\1 = 1030/' \
    -e 's/^\(controls_a\[CONTROL_INDEX_FAST_FORWARD\]\) = .*/\1 = 1036/' -e 's/^\(controls_a\[CONTROL_INDEX_MENU\]\) = .*/\1 = 1035/' "$IMG" > config/drastic.cfg
: > "$T/settings.conf"
sw=$(NDS_BUTTONS_SETTINGS=$T/settings.conf sh "$L/nds-buttons" drastic config/drastic.cfg)
check "$(keys a)" "$(keys a "$IMG")" "an old card's set a becomes the image's"
check "$sw" "6" "L2 swaps the screens (DSFLIP_SWAP_BUTTONS 6)"
cp config/drastic.cfg config/once; NDS_BUTTONS_SETTINGS=$T/settings.conf sh "$L/nds-buttons" drastic config/drastic.cfg >/dev/null
check "$(cmp -s config/drastic.cfg config/once && echo same)" "same" "twice changes nothing"
# a player's choice: L2 saves, R2 nothing
printf 'nds_btn_l2 = save\nnds_btn_r2 = none\n' > "$T/settings.conf"
sw=$(NDS_BUTTONS_SETTINGS=$T/settings.conf sh "$L/nds-buttons" drastic config/drastic.cfg)
check "$(keys a | grep -o 'SAVE_STATE=[0-9]*\|FAST_FORWARD=[0-9]*' | tr '\n' ' ')$sw" "SAVE_STATE=1030 FAST_FORWARD=65535 " "L2 = save, R2 = nothing: save on L2, no fast forward, no swap button"
rm -f config/drastic.cfg; NDS_BUTTONS_SETTINGS=$T/settings.conf sh "$L/nds-buttons" drastic config/drastic.cfg >/dev/null
check "$(ls config | grep -c drastic.cfg)" "0" "no drastic.cfg yet: nothing made"
cd - >/dev/null || exit 1

echo "nds-saves: an empty save never goes over one with something in it (#96)"
D=$T/data
# saves <dsv bytes|-> <sav bytes|-> <newer: dsv|sav>: a made-up card with those two files ('-': none), then nds-saves
saves() {
    rm -rf "$D"; mkdir -p "$D/drastic/backup" "$D/saves/nds"
    [ "$1" = - ] || head -c "$1" /dev/zero | tr '\0' d > "$D/drastic/backup/Game.dsv"
    [ "$2" = - ] || head -c "$2" /dev/zero | tr '\0' s > "$D/saves/nds/Game.sav"
    for f in "$D/drastic/backup/Game.dsv" "$D/saves/nds/Game.sav"; do [ -f "$f" ] && touch -d '2026-01-01 10:00' "$f"; done
    case $3 in dsv) touch -d '2026-01-01 11:00' "$D/drastic/backup/Game.dsv" ;; sav) touch -d '2026-01-01 11:00' "$D/saves/nds/Game.sav" ;; esac
    BANANA_ROOT=$D sh "$L/nds-saves" "/roms/nds/Game.nds" > "$T/log"
}
# what each holds now: <bytes><its letter>
held() { for f in "$D/drastic/backup/Game.dsv" "$D/saves/nds/Game.sav"; do [ -f "$f" ] && printf '%s%s ' "$(wc -c < "$f" | tr -d ' ')" "$(head -c 1 "$f")" || printf -- '- '; done; }
saves 8192 0 sav
check "$(held)" "8192d 8192d " "an empty .sav newer than an 8 KB .dsv: the .dsv kept and copied over it"
check "$(grep -c 'empty (0 bytes)' "$T/log")" "1" "...and said so"
saves 8192 100 sav
check "$(held)" "8192d 8192d " "a 100-byte .sav (no save) newer: the same"
saves 0 8192 dsv
check "$(held)" "8192s 8192s " "an empty .dsv newer than an 8 KB .sav: the .sav kept and copied over it"
saves 8192 512 sav
check "$(held)" "512s 512s " "a 512-byte .sav newer (a 4 Kbit EEPROM): taken, as before"
saves 512 8192 dsv
check "$(held)" "512d 512d " "a newer .dsv that is a save: taken, as before"
saves 8192 - x
check "$(held)" "8192d 8192d " "no .sav yet: made from the .dsv"
saves - 8192 x
check "$(held)" "8192s 8192s " "no .dsv yet: made from the .sav"
saves 8192 0 sav; saves_again=$(held)
BANANA_ROOT=$D sh "$L/nds-saves" "/roms/nds/Game.nds" > "$T/log"
check "$(held) $(wc -c < "$T/log" | tr -d ' ')" "$saves_again 0" "a second run: nothing more to do"
# a ROCKNIX save beside the game (#25): an empty one is not taken
rm -rf "$D"; mkdir -p "$T/roms"; : > "$T/roms/Game.dsv"
BANANA_ROOT=$D sh "$L/nds-saves" "$T/roms/Game.nds" > "$T/log"
check "$(held)" "- - " "an empty ROCKNIX .dsv beside the game is not taken"
head -c 8192 /dev/zero | tr '\0' r > "$T/roms/Game.dsv"
BANANA_ROOT=$D sh "$L/nds-saves" "$T/roms/Game.nds" > "$T/log"
check "$(held)" "8192r 8192r " "one with a save in it is"

# the save folders: RetroArch's per-core saves move to the one folder the Home server syncs; GBA stays in its own;
# system folders (nds, megadrive) and their files are left alone
D=$T/flat; mkdir -p "$D/data/saves/PicoDrive" "$D/data/saves/gpSP" "$D/data/saves/nds" "$D/data/saves/megadrive" "$D/run" "$D/etc"
printf '[gba]\n[nds]\n[megadrive]\n' > "$D/etc/systems.conf"
echo gen > "$D/data/saves/PicoDrive/Game.srm"; echo gba > "$D/data/saves/gpSP/Game.srm"
echo nds > "$D/data/saves/nds/Game.sav"; echo sys > "$D/data/saves/megadrive/Game.srm"
BANANA_DATA="$D/data" BANANA_RUN="$D/run" BANANA_SYSTEMS="$D/etc/systems.conf" sh "$L/saves-flat"
check "$(cat "$D/data/saves/Game.srm" 2>/dev/null)" "gen" "a core's save is flat, for the Home server"
check "$(cat "$D/data/saves/gba/Game.srm" 2>/dev/null)" "gba" "a GBA core's save is in saves/gba"
check "$(cat "$D/data/saves/nds/Game.sav" 2>/dev/null)" "nds" "a system folder's save is left alone"
check "$(cat "$D/data/saves/megadrive/Game.srm" 2>/dev/null)" "sys" "a system folder (megadrive) is left alone"
[ -f "$D/run/saves-flat" ] && [ -f "$D/data/banana/saves-legacy/PicoDrive/Game.srm" ] &&
    passed=$((passed + 1)) || { failed=$((failed + 1)); echo "  FAIL: no migration marker or backup"; }
[ ! -e "$D/data/saves/PicoDrive/Game.srm" ] && [ ! -e "$D/data/saves/gpSP/Game.srm" ] &&
    passed=$((passed + 1)) || { failed=$((failed + 1)); echo "  FAIL: a migrated core file stayed in saves/"; }
# PICO-8's cart data (saves/cdata) stays where it is; a flat save a newer core copy replaces is kept in saves-legacy
D=$T/flat2; mkdir -p "$D/data/saves/cdata" "$D/data/saves/Snes9x" "$D/run" "$D/etc"
printf '[gba]\n' > "$D/etc/systems.conf"
echo cart > "$D/data/saves/cdata/game.p8d.txt"
echo old > "$D/data/saves/Game.srm"; touch -t 202601010000 "$D/data/saves/Game.srm"
echo new > "$D/data/saves/Snes9x/Game.srm"
BANANA_DATA="$D/data" BANANA_RUN="$D/run" BANANA_SYSTEMS="$D/etc/systems.conf" sh "$L/saves-flat"
check "$(cat "$D/data/saves/cdata/game.p8d.txt" 2>/dev/null)" "cart" "PICO-8's cart data stays in saves/cdata"
check "$(cat "$D/data/saves/Game.srm" 2>/dev/null)" "new" "the newer core save is the flat one"
check "$(cat "$D/data/banana/saves-legacy/replaced/Game.srm" 2>/dev/null)" "old" "the flat save it replaced is kept"
# a blank save (only 0xff or only 0x00 bytes: a core that never got to write one) never goes over one with data in it,
# newer or not; a save with data goes over a blank one (fl: its first bytes, a blank one's as F or 0)
fl() { head -c 4 "$1" 2>/dev/null | tr '\377\000' F0; }
D=$T/flat3; mkdir -p "$D/data/saves/gpSP" "$D/data/saves/mGBA" "$D/data/saves/gba" "$D/data/saves/Snes9x" "$D/run" "$D/etc"
printf '[gba]\n' > "$D/etc/systems.conf"
echo real > "$D/data/saves/gpSP/Game.srm"; touch -t 202601010000 "$D/data/saves/gpSP/Game.srm"
head -c 1024 /dev/zero | tr '\0' '\377' > "$D/data/saves/mGBA/Game.srm"
head -c 1024 /dev/zero > "$D/data/saves/Snes9x/Other.srm"
echo real > "$D/data/saves/Other.srm"; touch -t 202601010000 "$D/data/saves/Other.srm"
head -c 1024 /dev/zero > "$D/data/saves/gba/Third.srm"
echo real > "$D/data/saves/gpSP/Third.srm"; touch -t 202601010000 "$D/data/saves/gpSP/Third.srm"
BANANA_DATA="$D/data" BANANA_RUN="$D/run" BANANA_SYSTEMS="$D/etc/systems.conf" sh "$L/saves-flat"
check "$(fl "$D/data/saves/gba/Game.srm")" "real" "a newer blank 0xff save doesn't go over a real one"
check "$(fl "$D/data/saves/Other.srm")" "real" "a newer blank 0x00 save doesn't go over a real flat one"
check "$(fl "$D/data/saves/gba/Third.srm")" "real" "an older real save goes over a newer blank one"
check "$(wc -c < "$D/data/banana/saves-legacy/mGBA/Game.srm" 2>/dev/null)" "1024" "the blank one is kept in saves-legacy"
# a core that runs more than one system (mGBA: GBA, GB and GBC): each save goes where its game's system keeps them, by
# where the game is (either card, a subfolder too); a save with no game found stays the GBA's
D=$T/flat4; mkdir -p "$D/data/saves/mGBA" "$D/data/roms/gb" "$D/data/roms/gbc/Zelda" "$D/data/roms/gba" "$D/sd2/roms/gb" "$D/run/sd2" "$D/etc"
printf '[gba]\n[gb]\n[gbc]\n' > "$D/etc/systems.conf"; echo "$D/sd2/roms" > "$D/run/sd2/roms"
touch "$D/data/roms/gb/Tetris (World) (Rev 1).gb" "$D/data/roms/gbc/Zelda/Zelda [DX] (USA).gbc" "$D/data/roms/gba/Pokemon Emerald (USA).gba" "$D/sd2/roms/gb/Kirby's Dream Land (USA, Europe).zip"
for g in "Tetris (World) (Rev 1)" "Zelda [DX] (USA)" "Pokemon Emerald (USA)" "Kirby's Dream Land (USA, Europe)" "No Game Here"; do echo "$g" > "$D/data/saves/mGBA/$g.srm"; done
BANANA_DATA="$D/data" BANANA_RUN="$D/run" BANANA_SYSTEMS="$D/etc/systems.conf" sh "$L/saves-flat"
check "$(cat "$D/data/saves/Tetris (World) (Rev 1).srm" 2>/dev/null)" "Tetris (World) (Rev 1)" "mGBA's Game Boy save is flat (roms/gb)"
check "$(cat "$D/data/saves/Zelda [DX] (USA).srm" 2>/dev/null)" "Zelda [DX] (USA)" "mGBA's Game Boy Color save is flat (a subfolder of roms/gbc)"
check "$(cat "$D/data/saves/Kirby's Dream Land (USA, Europe).srm" 2>/dev/null)" "Kirby's Dream Land (USA, Europe)" "a game on the second card counts too"
check "$(cat "$D/data/saves/gba/Pokemon Emerald (USA).srm" 2>/dev/null)" "Pokemon Emerald (USA)" "mGBA's GBA save is in saves/gba"
check "$(cat "$D/data/saves/gba/No Game Here.srm" 2>/dev/null)" "No Game Here" "a save with no game found stays the GBA's"
check "$(ls "$D/data/saves/gba" | wc -l)" "2" "no Game Boy save in saves/gba"
# two core folders with a save of the same name: one game (one system has it: the newer save is flat) or two games of
# different systems, or none found to tell (both left where they are, said in the log, tried again at the next boot)
D=$T/flat5; mkdir -p "$D/data/saves/FCEUmm" "$D/data/saves/Gambatte" "$D/data/saves/Snes9x" "$D/data/saves/bsnes" \
    "$D/data/roms/nes" "$D/data/roms/gb" "$D/data/roms/snes" "$D/run" "$D/etc"
printf '[nes]\n[gb]\n[snes]\n' > "$D/etc/systems.conf"
touch "$D/data/roms/nes/Pac-Man (USA).nes" "$D/data/roms/gb/Pac-Man (USA).gb" "$D/data/roms/snes/Mario (USA).sfc"
echo nes > "$D/data/saves/FCEUmm/Pac-Man (USA).srm"; touch -t 202601010000 "$D/data/saves/FCEUmm/Pac-Man (USA).srm"
echo gb > "$D/data/saves/Gambatte/Pac-Man (USA).srm"
echo nes > "$D/data/saves/FCEUmm/Unknown.srm"; echo gb > "$D/data/saves/Gambatte/Unknown.srm"
echo old > "$D/data/saves/Snes9x/Mario (USA).srm"; touch -t 202601010000 "$D/data/saves/Snes9x/Mario (USA).srm"
echo new > "$D/data/saves/bsnes/Mario (USA).srm"
BANANA_DATA="$D/data" BANANA_RUN="$D/run" BANANA_SYSTEMS="$D/etc/systems.conf" sh "$L/saves-flat" 2> "$T/log"
check "$(cat "$D/data/saves/FCEUmm/Pac-Man (USA).srm" "$D/data/saves/Gambatte/Pac-Man (USA).srm" 2>/dev/null | tr '\n' ' ')" "nes gb " \
    "two systems' saves of one name both stay in their core folders"
check "$([ -e "$D/data/saves/Pac-Man (USA).srm" ] && echo flat || echo none)" "none" "neither goes flat over the other"
check "$(cat "$D/data/saves/FCEUmm/Unknown.srm" "$D/data/saves/Gambatte/Unknown.srm" 2>/dev/null | tr '\n' ' ')" "nes gb " \
    "with no game found to tell, both stay too"
check "$(cat "$D/data/saves/Mario (USA).srm" 2>/dev/null)" "new" "one system's game in two cores: the newer save is flat"
check "$(grep -c 'Pac-Man (USA).srm' "$T/log")" "1" "the clash is said in the log, once"
check "$([ -f "$D/run/saves-flat" ] && echo marked || echo again)" "again" "no marker: tried again at the next boot"

echo "run-nds: a DSi game goes to BananaDS whatever Settings say, zipped too"
# (the launcher with its paths in a made-up root: the two emulators' launchers only say which one would run)
D=$T/nds; mkdir -p "$D/lib" "$D/banana" "$D/roms/in/sub"
printf '#!/bin/sh\necho superdrastic\n' > "$D/lib/run-drastic"; printf '#!/bin/sh\necho bananads\n' > "$D/lib/run-bananads"; chmod +x "$D/lib/"*
sed "s|/usr/libexec/banana/|$D/lib/|g; s|/data/banana/|$D/banana/|g" "$L/run-nds" > "$D/run-nds"
echo 'nds_emulator = superdrastic' > "$D/banana/settings.conf"
# header <file> <unit code>: a ROM's first 512 bytes with that unit code at 0x12 (3: DSi only, 0: DS)
header() { { head -c 18 /dev/zero; printf "\\$(printf %03o "$2")"; head -c 493 /dev/zero; } > "$1"; }
header "$D/roms/DSi Game (USA).nds" 3; header "$D/roms/DS Game (USA).nds" 0
header "$D/roms/in/sub/DSi Game [b] (USA).NDS" 3
(cd "$D/roms/in" && zip -q -r "../DSi Game [b] (USA).zip" sub) && rm -rf "$D/roms/in/sub"
header "$D/roms/in/DS Game (USA).nds" 0; (cd "$D/roms/in" && zip -q "../DS Game (USA).zip" "DS Game (USA).nds")
printf x > "$D/roms/in/Ware (USA).dsi"; (cd "$D/roms/in" && zip -q "../DSiWare (USA).zip" "Ware (USA).dsi")
nds() { sh "$D/run-nds" "$D/roms/$1" 2>/dev/null; }
check "$(nds "DSi Game (USA).nds") $(nds "DS Game (USA).nds")" "bananads superdrastic" "a DSi .nds to BananaDS, a DS one as Settings say"
check "$(nds "DSi Game [b] (USA).zip")" "bananads" "a zipped DSi-only .nds (in a folder in the zip) to BananaDS"
check "$(nds "DSiWare (USA).zip")" "bananads" "a zipped .dsi to BananaDS"
check "$(nds "DS Game (USA).zip")" "superdrastic" "a zipped DS game as Settings say"

# a full card (writes past 2 KB fail: ulimit -f 4 with SIGXFSZ ignored, so a write returns an error as ENOSPC does and
# nothing is killed for it - busybox awk then goes on and exits 0): nothing a script rewrites is left cut short, and
# a backup it makes never replaces a whole one with a piece
echo "a full card: nds-saves, nds-buttons and nds-reset leave the files whole"
# card <newer: dsv|sav>: both files 8 KB (the .dsv d, the .sav s), that one newer, and nds-saves not run yet
card() {
    rm -rf "$D"; mkdir -p "$D/drastic/backup" "$D/saves/nds"
    head -c 8192 /dev/zero | tr '\0' d > "$D/drastic/backup/Game.dsv"; head -c 8192 /dev/zero | tr '\0' s > "$D/saves/nds/Game.sav"
    touch -d '2026-01-01 10:00' "$D/drastic/backup/Game.dsv" "$D/saves/nds/Game.sav"
    case $1 in dsv) touch -d '2026-01-01 11:00' "$D/drastic/backup/Game.dsv" ;; sav) touch -d '2026-01-01 11:00' "$D/saves/nds/Game.sav" ;; esac
}
card dsv
cp -p "$D/saves/nds/Game.sav" "$D/saves/nds/Game.sav.bak"
( trap "" XFSZ; ulimit -f 4; BANANA_ROOT=$D sh "$L/nds-saves" "/roms/nds/Game.nds" > "$T/log" 2>&1 )
check "$(held)" "8192d 8192s " "nds-saves, DraStic's newer: BananaDS's .sav kept whole on a full card"
check "$(wc -c < "$D/saves/nds/Game.sav.bak" | tr -d ' ')" "8192" "...and its .bak too"
card sav
cp -p "$D/drastic/backup/Game.dsv" "$D/drastic/backup/Game.dsv.bak"
( trap "" XFSZ; ulimit -f 4; BANANA_ROOT=$D sh "$L/nds-saves" "/roms/nds/Game.nds" > "$T/log" 2>&1 )
check "$(held)" "8192d 8192s " "nds-saves, BananaDS's newer: DraStic's .dsv kept whole on a full card"
check "$(wc -c < "$D/drastic/backup/Game.dsv.bak" | tr -d ' ')" "8192" "...and its .bak too"
mkdir -p "$T/btn"; cp "$R/buildroot/board/rgds/drastic.cfg" "$T/btn/drastic.cfg"; printf 'nds_btn_l2 = a\nnds_btn_r2 = b\nnds_btn_l3 = x\nnds_btn_r3 = y\n' > "$T/btn/settings.conf"
want=$(wc -c < "$T/btn/drastic.cfg" | tr -d ' ')
cp "$T/btn/drastic.cfg" "$T/btn/free.cfg"; NDS_BUTTONS_SETTINGS=$T/btn/settings.conf sh "$L/nds-buttons" drastic "$T/btn/free.cfg" > /dev/null 2>&1
cmp -s "$T/btn/drastic.cfg" "$T/btn/free.cfg" && check same differs "(the test itself: these buttons change drastic.cfg)"
( trap "" XFSZ; ulimit -f 4; NDS_BUTTONS_SETTINGS=$T/btn/settings.conf sh "$L/nds-buttons" drastic "$T/btn/drastic.cfg" > /dev/null 2>&1 )
check "$(wc -c < "$T/btn/drastic.cfg" | tr -d ' ')" "$want" "nds-buttons: drastic.cfg not cut short on a full card"
mkdir -p "$T/reset"; head -c 4000 /dev/zero | tr '\0' '#' | fold -w 79 > "$T/reset/settings.conf"; printf '\nnds_btn_r2 = save\n' >> "$T/reset/settings.conf"
want=$(wc -c < "$T/reset/settings.conf" | tr -d ' ')
cp "$T/reset/settings.conf" "$T/reset/free.conf"; NDS_RESET_SETTINGS=$T/reset/free.conf sh "$L/nds-reset" ds-buttons > /dev/null 2>&1
grep -q '^nds_btn_r2 = ff$' "$T/reset/free.conf" || check same differs "(the test itself: ds-buttons resets R2)"
( trap "" XFSZ; ulimit -f 4; NDS_RESET_SETTINGS=$T/reset/settings.conf sh "$L/nds-reset" ds-buttons > /dev/null 2>&1 )
check "$(wc -c < "$T/reset/settings.conf" | tr -d ' ')" "$want" "nds-reset ds-buttons: settings.conf not cut short on a full card"
cp "$R/buildroot/board/rgds/drastic.cfg" "$T/reset/def.cfg"; sed 's/^\(controls_a\[[^]]*\] = \).*/\165535/' "$T/reset/def.cfg" > "$T/reset/drastic.cfg"
want=$(wc -c < "$T/reset/drastic.cfg" | tr -d ' ')
cp "$T/reset/drastic.cfg" "$T/reset/free.cfg"; NDS_RESET_DEF=$T/reset/def.cfg NDS_RESET_CFG=$T/reset/free.cfg sh "$L/nds-reset" buttons > /dev/null 2>&1
cmp -s "$T/reset/free.cfg" "$T/reset/def.cfg" || check same differs "(the test itself: buttons puts the image's controls back)"
( trap "" XFSZ; ulimit -f 4; NDS_RESET_DEF=$T/reset/def.cfg NDS_RESET_CFG=$T/reset/drastic.cfg sh "$L/nds-reset" buttons > /dev/null 2>&1 )
check "$(wc -c < "$T/reset/drastic.cfg" | tr -d ' ')" "$want" "nds-reset buttons: drastic.cfg not cut short on a full card"

echo "launchers-test: $passed passed, $failed failed"
[ $failed = 0 ]

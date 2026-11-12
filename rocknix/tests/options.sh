#!/bin/sh
# usr/libexec/banana/rocknix-options against sample es_systems.cfg / es_features.cfg: a game's and a system's rows (the
# emulator first, then the options its emulator and core have), choices kept where EmulationStation keeps them in
# ROCKNIX's settings, and run-rocknix starting the game with the chosen emulator and core.
HERE=$(cd "$(dirname "$0")/.." && pwd)
L=$HERE/../system/rootfs/usr/libexec/banana
O=$L/rocknix-options
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
fail=0
bad() { echo "FAIL rocknix options: $*"; fail=1; }
export ROCKNIX_SETTINGS="$T/system.cfg" ROCKNIX_LOCK="$T/lock" ROCKNIX_ES_SYSTEMS="$HERE/tests/es_systems.cfg" \
    ROCKNIX_ES_FEATURES="$HERE/tests/es_features.cfg" ROCKNIX_SHADERS="$T/shaders"
mkdir -p "$T/shaders/crt"; : > "$T/shaders/crt/crt-easy.glslp"
printf 'global.rewind=0\nnds.screen_layout=1\n' > "$T/system.cfg"
keys() { "$O" rows "$@" | cut -f1 | tr '\n' ' '; }
# the GBA's default (RetroArch's mgba): the shared options, its own ones
[ "$(keys gba Zelda.gba)" = "emulator ratio integerscale smooth shaderset bezel runahead rewind autosave retroachievements " ] || bad "gba rows: $(keys gba Zelda.gba)"
"$O" rows gba - | grep -q "	crt / crt-easy	crt/crt-easy.glslp" || bad "no shader from the folder"
# the DS's: the standalone melonDS's own option, and what the system says over the game
[ "$(keys nds a.nds)" = "emulator retroachievements screen_layout " ] || bad "nds rows: $(keys nds a.nds)"
"$O" rows nds a.nds | grep -q "^screen_layout	Screen layout	[^	]*		As the system's (Top emphasis)	Even	0" || bad "the system's screen layout not shown"
# a choice for the system and for one game
"$O" set gba - emulator retroarch/gpsp
"$O" set gba "Zelda.gba" ratio 16/9
grep -qx 'gba.emulator=retroarch' "$T/system.cfg" && grep -qx 'gba.core=gpsp' "$T/system.cfg" || bad "the system's emulator not kept"
grep -qxF 'gba["Zelda.gba"].ratio=16/9' "$T/system.cfg" || bad "the game's ratio not kept"
[ "$(keys gba Zelda.gba)" = "emulator ratio integerscale smooth shaderset bezel runahead rewind " ] || bad "gpsp's rows: $(keys gba Zelda.gba)"
[ "$("$O" resolve gba Zelda.gba retroarch mgba)" = "retroarch gpsp" ] || bad "resolve: the system's"
"$O" set gba "Zelda.gba" emulator retroarch/mgba
[ "$("$O" resolve gba Zelda.gba retroarch mgba)" = "retroarch mgba" ] || bad "resolve: the game's"
[ "$("$O" resolve gba Other.gba retroarch mgba)" = "retroarch gpsp" ] || bad "resolve: another game"
"$O" set gba "Zelda.gba" ratio
grep -qF 'Zelda.gba"].ratio' "$T/system.cfg" && bad "an emptied choice stayed"
grep -qx 'global.rewind=0' "$T/system.cfg" || bad "another setting was lost"
"$O" set 'gba;x' - ratio 4/3 2>/dev/null && bad "a bad system name was taken"
# run-rocknix: the game's emulator and core in place of the run line's default
mkdir -p "$T/bin" "$T/run" "$T/lib"
printf '#!/bin/sh\necho "runemu $*" >> "%s/log"\n' "$T" > "$T/bin/runemu.sh"; chmod +x "$T/bin/runemu.sh"
for s in rocknix-options; do ln -s "$L/$s" "$T/lib/$s"; done
printf '#!/bin/sh\n:\n' > "$T/lib/backlight"; cp "$T/lib/backlight" "$T/lib/handheld"; chmod +x "$T/lib/backlight" "$T/lib/handheld"
printf 'ra_user=player1\nra_password=secret\n' > "$T/accounts.conf"
env BANANA_LIBEXEC="$T/lib" BANANA_RUNEMU="$T/bin/runemu.sh" BANANA_RUN="$T/run" BANANA_DATA="$T" \
    sh "$L/run-rocknix" gba "/roms/gba/Other.gba" --core=mgba --emulator=retroarch --controllers=""
grep -qF 'runemu /roms/gba/Other.gba -Pgba --core=gpsp --emulator=retroarch --controllers=' "$T/log" || bad "run-rocknix: $(cat "$T/log")"
grep -qx 'global.retroachievements.username=player1' "$T/system.cfg" && grep -qx 'global.retroachievements=1' "$T/system.cfg" ||
    bad "the RetroAchievements account not given to ROCKNIX"
[ $fail = 0 ] && echo "rocknix options: all passed"
exit $fail

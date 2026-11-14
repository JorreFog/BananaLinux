#!/bin/bash
# SPDX-License-Identifier: MIT
# BananaOS's menu as ROCKNIX's frontend (bananasway.service, under sway as essway's EmulationStation is): ROCKNIX's
# systems (EmulationStation's es_systems.cfg) as the menu's shelves, its games through runemu.sh, both screens as
# windows on sway's two outputs. The menu's data (settings, play stats, favourites) in /storage/.config/banana-data.

# the same setup EmulationStation's start gets (sway's outputs, the device's quirks)
[ -x /usr/bin/es_settings ] && . /usr/bin/es_settings

# /data (the package's link to /storage/.config/banana-data): the menu's and its apps' folders, ROCKNIX's roms and bios
D=/storage/.config/banana-data
mkdir -p "${D}/banana" "${D}/store" "${D}/apps" "${D}/home" /storage/roms/bios /storage/roms/screenshots
for l in roms:/storage/roms bios:/storage/roms/bios screenshots:/storage/roms/screenshots saves:/storage/roms/savestates; do
  [ -e "${D}/${l%%:*}" ] || ln -sfn "${l#*:}" "${D}/${l%%:*}"
done
DATA=/data/banana
RUN=/run/banana
export BANANA_DATA=$DATA BANANA_RUN=$RUN
mkdir -p "${DATA}/logs" "${RUN}"
# the handheld's board and hardware profile (/etc/banana/board and hw.conf link to /run/banana's): board thor on the
# AYN Thor, which gives the menu the Thor's defaults (the Thor OLED theme, its name, the setup's Controls, Home server
# and Steam slides) and hw-thor.conf's lines (the Thor button: HW_HOME, HW_HOME_KEY; the fan and lights; USB debugging)
BOARD=$(/usr/libexec/banana/rocknix-device 2>/dev/null)
# the volume keys' and Settings' volume: the server's default sink (the volume script's PipeWire branch)
echo "pipewire @DEFAULT_AUDIO_SINK@" > "$RUN/mixer"
LOG=${DATA}/logs/banana-ui.log
[ -f "${LOG}" ] && mv -f "${LOG}" "${LOG}.1"

# ROCKNIX's systems: the user's es_systems.cfg if there is one, else the image's
ES=/storage/.config/emulationstation/es_systems.cfg
[ -f "${ES}" ] || ES=/usr/config/emulationstation/es_systems.cfg
python3 /usr/share/banana/rocknix/es-systems-to-banana.py "${ES}" "${RUN}/systems.conf" 2>>"${LOG}"

# the two screens' backlights (which is the top: the DSI hosts' order, usr/libexec/banana/backlight), each at the
# level the menu keeps for it
set -- $(BANANA_TOP="${WLR_CON:-DSI-2}" /usr/libexec/banana/backlight names)
[ "$1" != - ] && export BANANA_BACKLIGHT_TOP="$1"
[ "$2" != - ] && export BANANA_BACKLIGHT_BOTTOM="$2"
/usr/libexec/banana/backlight restore 2>>"${LOG}"

# the stick lights and the GPU boost as Settings > Sleep & power > Fan, lights & speed keeps them (the fan's profile is
# ROCKNIX's own setting, which its fancontrol reads at boot)
conf() { sed -n "s/^[[:space:]]*$1[[:space:]]*=[[:space:]]*//p" "${DATA}/settings.conf" 2>/dev/null | tail -n 1; }
/usr/libexec/banana/handheld leds "$(conf leds || true)" >/dev/null 2>&1
[ "$(conf gpu_boost)" = on ] && /usr/libexec/banana/handheld gpu on >/dev/null 2>&1
# Sleep & power's one row here: asleep this long, the handheld turns off (ROCKNIX's timed shutdown, 15 minutes as it
# ships: the menu's row starts there)
if [ -z "$(conf poweroff_after_sleep_minutes)" ]; then
  echo "poweroff_after_sleep_minutes = 15" >> "${DATA}/settings.conf"
else
  /usr/libexec/banana/handheld poweroff-after "$(conf poweroff_after_sleep_minutes)" >/dev/null 2>&1
fi
# the buttons and touch: the bottom touchscreen by its name (the Thor's device tree labels); the face buttons as
# Settings > System > Confirm button says (the right one, as on the RG DS, or the bottom one); while ROCKNIX's
# InputPlumber merges the pad and the AYN key into its virtual pad, the menu reads that one only
export BANANA_TOUCH=bottom_touchscreen
[ "$(conf confirm_button)" = bottom ] && export BANANA_CONFIRM=south
systemctl is-active -q inputplumber 2>/dev/null && export BANANA_INPUT_IGNORE="rsinput-gamepad,gpio-keys-ayn"
# Settings' rows for a frontend whose games run through run-rocknix
export BANANA_ROCKNIX=1

# Banana's emulators' and apps' windows full screen, each on its screen (their bottom windows' titles end in "Bottom":
# ROCKNIX's own rule moves those to the second output)
swaymsg 'for_window [title="^(BananaDS|BananaFront|Activity|Store|Döda Kvarter)( Bottom)?$"] fullscreen enable' >/dev/null 2>&1
# a RetroArch game on the top screen, while the menu keeps the bottom one for its game panel
swaymsg "for_window [app_id=\"(?i)retroarch\"] move window to output ${WLR_CON:-DSI-2}" >/dev/null 2>&1

# PC games (pc-games, steam, run-pc): their entries and folders in roms/windows (ROCKNIX's roms/pc is DOS's), Wine's
# runtime and prefix in /data/pc on /storage's ext4 (pc-games' folder mode: no image to mount)
export BANANA_PC_ROMS=/storage/roms/windows
mkdir -p /storage/roms/windows "${D}/pc"
# a PC game's window (Wine names it after its program) and the terminal the PC games' setup and Steam downloads show
# in (run-pc), full screen on the top screen
swaymsg "for_window [app_id=\"(?i)(.*\\.exe|foot)\$\"] move window to output ${WLR_CON:-DSI-2}, fullscreen enable" >/dev/null 2>&1

# the second output: ROCKNIX's sway config for the AYN Thor starts it powered off and its touchscreen ignored, for
# EmulationStation's bottom app to wake (a window titled "...Bottom..."); the menu is on it from the start
SECOND=${SDL_VIDEO_DISPLAY_PRIORITY#*,}
[ "${SECOND}" = "${SDL_VIDEO_DISPLAY_PRIORITY}" ] && SECOND=
[ -n "${SECOND}" ] || SECOND=$(swaymsg -t get_outputs 2>/dev/null | jq -r ".[] | select(.name != \"${WLR_CON:-DSI-2}\") | .name" 2>/dev/null | head -n 1)
if [ -n "${SECOND}" ]; then
  swaymsg "output ${SECOND} power on" >/dev/null 2>&1
  swaymsg 'input "0:0:bottom_touchscreen" events enabled' >/dev/null 2>&1
  export BANANA_BOTTOM_OUTPUT="${SECOND}"
fi

# Hooks for the Thor button over a game (banana-ui): hw.conf's HW_HOME=yes and HW_HOME_KEY (the AYN Thor's lines,
# hw-thor.conf) turn it on and name the keys; BANANA_HOME_KEY=1 here would force it on for another board. A game's
# processes are not under this unit: runemu.sh moves each into a scope of ROCKNIX's games.slice (hw.conf's
# HW_GAME_CGROUP); BANANA_GAME_CG would name the group the menu freezes. BANANA_BOTTOM_OUTPUT names the second output.
# (${BOARD}: thor or rocknix)
# banana-ui talks to sway through its IPC socket (the menu raised and fullscreen over a game, the game's views and
# focus put back after)
[ -n "$SWAYSOCK" ] || export SWAYSOCK=$(ls /run/user/*/sway-ipc.*.sock /run/sway-ipc.*.sock 2>/dev/null | head -n 1)

# both screens as windows (src/kms_sdl.cpp): the top one on sway's main output (WLR_CON, the 095-sway profile's),
# the other on the second
export BANANA_DISPLAY=sdl SDL_VIDEODRIVER=wayland
export BANANA_TOP="${WLR_CON:-DSI-2}"

exec /usr/bin/banana-ui --systems "${RUN}/systems.conf" --roms /data/roms --data "${DATA}" >>"${LOG}" 2>&1

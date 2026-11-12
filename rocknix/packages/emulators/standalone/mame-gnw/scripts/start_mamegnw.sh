#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# A Game & Watch on ROCKNIX (runemu.sh: start_mamegnw.sh <rom> <system>), as BananaOS's run-gw starts one: MAME's
# hh_sm510 driver, the game's artwork beside the ROMs (artwork/<game>.zip), saves and states in ROCKNIX's folders.
# Under sway: a one-screen game is one full-screen window on the top screen; a two-screen one (Donkey Kong, Zelda...)
# is two, the second ("... Screen 1") moved to the second output by a rule set here. ROCKNIX's exit hotkey stops it.

. /etc/profile
set_kill set "mame-gnw"

ROM="${1}"
GAME=$(basename "${ROM}" .zip)
ROMS=$(dirname "${ROM}")
ART="${ROMS}/artwork"
D=/storage/.config/mame-gnw
mkdir -p "${D}/cfg" "${D}/ini" /storage/roms/savestates/gameandwatch /storage/roms/gameandwatch/saves

# the game's screens (MAME's own description of it): one, two stacked, or two side by side
SCREENS=$(/usr/bin/mame-gnw "${GAME}" -rompath "${ROMS}" -listxml 2>/dev/null | grep -c '<display ')
[ "${SCREENS:-0}" -ge 1 ] || { echo "start_mamegnw: MAME doesn't know ${GAME}"; exit 1; }
VIEW="Background"
unzip -l "${ART}/${GAME}.zip" 2>/dev/null | grep -q '\.lay$' || VIEW="auto"

ARGS=()
if [ "${SCREENS}" -ge 2 ] && [ "${DEVICE_HAS_DUAL_SCREEN}" = "true" ]; then
  # a window per screen: the second on sway's second output (SDL_VIDEO_DISPLAY_PRIORITY: ROCKNIX's "top,bottom")
  second="${SDL_VIDEO_DISPLAY_PRIORITY#*,}"
  [ -n "${second}" ] && [ "${second}" != "${SDL_VIDEO_DISPLAY_PRIORITY}" ] &&
    swaymsg "for_window [title=\".*\\[${GAME}\\].*Screen 1\"] move window to output ${second}, fullscreen enable" >/dev/null 2>&1
  ARGS+=(-numscreens 2 -view0 auto -view1 auto)
else
  ARGS+=(-view "${VIEW}")
fi

export SDL_VIDEODRIVER=wayland BANANA_NO_WARNINGS=1 BANANA_SHORT_MENU=1 HOME="${D}"
cd "${D}"
${EMUPERF} /usr/bin/mame-gnw "${GAME}" \
  -rompath "${ROMS}" -artpath "${ART}" -inipath "${D}/ini" -cfg_directory "${D}/cfg" \
  -nvram_directory /storage/roms/gameandwatch/saves -state_directory /storage/roms/savestates/gameandwatch \
  -snapshot_directory /storage/roms/screenshots -homepath "${D}" \
  -skip_gameinfo -nowindow -video accel -joystick -joystickprovider sdlgame -sound sdl "${ARGS[@]}"

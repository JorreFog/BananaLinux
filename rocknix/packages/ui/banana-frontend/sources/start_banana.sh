#!/bin/bash
# SPDX-License-Identifier: MIT
# BananaOS's menu as ROCKNIX's frontend (bananasway.service, under sway as essway's EmulationStation is): ROCKNIX's
# systems (EmulationStation's es_systems.cfg) as the menu's shelves, its games through runemu.sh, both screens as
# windows on sway's two outputs. The menu's data (settings, play stats, favourites) in /storage/.config/banana.

# the same setup EmulationStation's start gets (sway's outputs, the device's quirks)
[ -x /usr/bin/es_settings ] && . /usr/bin/es_settings

DATA=/storage/.config/banana
RUN=/run/banana
mkdir -p "${DATA}/logs" "${RUN}"
LOG=${DATA}/logs/banana-ui.log
[ -f "${LOG}" ] && mv -f "${LOG}" "${LOG}.1"

# ROCKNIX's systems: the user's es_systems.cfg if there is one, else the image's
ES=/storage/.config/emulationstation/es_systems.cfg
[ -f "${ES}" ] || ES=/usr/config/emulationstation/es_systems.cfg
python3 /usr/share/banana/rocknix/es-systems-to-banana.py "${ES}" "${RUN}/systems.conf" 2>>"${LOG}"

# both screens as windows (src/kms_sdl.cpp): the top one on sway's main output (WLR_CON, the 095-sway profile's),
# the other on the second
export BANANA_DISPLAY=sdl SDL_VIDEODRIVER=wayland
export BANANA_TOP="${WLR_CON:-DSI-2}"

exec /usr/bin/banana-ui --systems "${RUN}/systems.conf" --roms /storage/roms --data "${DATA}" >>"${LOG}" 2>&1

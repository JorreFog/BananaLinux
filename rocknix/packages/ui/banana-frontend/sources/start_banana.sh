#!/bin/bash
# SPDX-License-Identifier: MIT
# BananaOS's menu as ROCKNIX's frontend (bananasway.service, under sway as essway's EmulationStation is): ROCKNIX's
# systems (EmulationStation's es_systems.cfg) as the menu's shelves, its games through runemu.sh, both screens as
# windows on sway's two outputs. The menu's data (settings, play stats, favourites) in /storage/.config/banana.

# the same setup EmulationStation's start gets (sway's outputs, the device's quirks)
[ -x /usr/bin/es_settings ] && . /usr/bin/es_settings

DATA=/data/banana	# (/storage/.config/banana: the package's /data links)
RUN=/run/banana
export BANANA_DATA=$DATA BANANA_RUN=$RUN
mkdir -p "${DATA}/logs" "${RUN}"
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
# the buttons and touch: the bottom touchscreen by its name (the Thor's device tree labels); the face buttons as
# Settings > System > Confirm button says (the right one, as on the RG DS, or the bottom one); while ROCKNIX's
# InputPlumber merges the pad and the AYN key into its virtual pad, the menu reads that one only
export BANANA_TOUCH=bottom_touchscreen
[ "$(conf confirm_button)" = bottom ] && export BANANA_CONFIRM=south
systemctl is-active -q inputplumber 2>/dev/null && export BANANA_INPUT_IGNORE="rsinput-gamepad,gpio-keys-ayn"
# Settings' rows for a frontend whose games run through run-rocknix
export BANANA_ROCKNIX=1

# both screens as windows (src/kms_sdl.cpp): the top one on sway's main output (WLR_CON, the 095-sway profile's),
# the other on the second
export BANANA_DISPLAY=sdl SDL_VIDEODRIVER=wayland
export BANANA_TOP="${WLR_CON:-DSI-2}"

exec /usr/bin/banana-ui --systems "${RUN}/systems.conf" --roms /data/roms --data "${DATA}" >>"${LOG}" 2>&1

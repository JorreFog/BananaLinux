#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# BananaDS on ROCKNIX (runemu.sh: start_bananads.sh <rom> <system>): its two SDL windows full screen, one on each of
# sway's outputs, the pad through InputPlumber, its settings and saves in /storage/.config/bananads, the DS BIOS
# files (optional: it has its own) in ROCKNIX's bios folder. ROCKNIX's exit hotkey sends SIGTERM: SDL takes it as a
# quit and the game's state is kept for the next start.

. /etc/profile
set_kill set "bananads"

ROM="${1}"
GAME="${ROM##*/}"
PLATFORM="${2:-nds}"
DATA=/storage/.config/bananads
mkdir -p "${DATA}"

# ROCKNIX's per-game options for it (the menu's game page: rocknix-options), as BananaDS settings
SETS=()
RES=$(get_setting internal_resolution "${PLATFORM}" "${GAME}")
[[ "${RES}" =~ ^[1-4]$ ]] && SETS+=(--set "render.scale=${RES}")

export SDL_VIDEODRIVER=wayland BANANADS_DATA="${DATA}"
${EMUPERF} /usr/bin/bananads --config "${DATA}/bananads.conf" --set bios=/storage/roms/bios \
  --set frontend=sdl --set sdl.windows=2 "${SETS[@]}" "${ROM}"

#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# A game in BananaFront on ROCKNIX (runemu.sh: start_bananafront.sh <rom> <system>). Its libretro core: the one the
# menu's run-rocknix names (BANANA_CORE: bananafront-<core>), else the game's or system's choice in ROCKNIX's settings,
# else the system's usual one. Saves beside the game (as ROCKNIX's RetroArch keeps them), states in ROCKNIX's
# savestates folder, the game's state kept at quit and taken up at the next start. ROCKNIX's exit hotkey stops it.

. /etc/profile
set_kill set "bananafront"

ROM="${1}"
GAME="${ROM##*/}"
PLATFORM="${2}"
CORE="${BANANA_CORE:-$(get_setting core "${PLATFORM}" "${GAME}")}"
case ${CORE} in bananafront-*) CORE=${CORE#bananafront-} ;; *) CORE= ;; esac
if [ -z "${CORE}" ]; then
  case ${PLATFORM} in
    gb|gbc|gbh|sgb) CORE=gambatte ;;
    gba) CORE=bananagba ;;
    nes|famicom|fds) CORE=banananes ;;
    snes|sfc|snesh|sufami|satellaview) CORE=bananasnes ;;
    megadrive|genesis|mastersystem|gamegear|sg-1000|segacd|sega32x) CORE=picodrive ;;
    psx) CORE=pcsx_rearmed ;;
    *) echo "start_bananafront: no core for ${PLATFORM}"; exit 1 ;;
  esac
fi
SO=/usr/lib/libretro/${CORE}_libretro.so
[ -f "${SO}" ] || SO=/tmp/cores/${CORE}_libretro.so

DATA=/storage/.config/bananafront
STATES=/storage/roms/savestates/${PLATFORM}
mkdir -p "${DATA}" "${STATES}"
export BANANAFRONT_DATA="${DATA}" SDL_VIDEODRIVER=wayland
${EMUPERF} /usr/bin/bananafront --system "${PLATFORM}" --saves "$(dirname "${ROM}")" --states "${STATES}" \
  --bios /storage/roms/bios --shots /storage/roms/screenshots --resume-load --resume-save --lowlat \
  "${SO}" "${ROM}"

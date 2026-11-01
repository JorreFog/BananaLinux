#!/bin/bash
# ra-ab.sh <plus|rgds> <seconds> <tag> [core.so]: the stress ROM through the real menu + RetroArch (today's path) with full
# data collection (stress-run-nes.sh). With a core: it is put in /data/retroarch/cores and chosen for NES through
# settings.conf's core.nes for the run only (file backed up and put back). Run under locked.sh.
D=$1; SEC=$2; TAG=$3; CORE=${4:-}
H=$(cd "$(dirname "$0")" && pwd)
dev() { ~/bananaos-0.4.3/dev.sh "$D" "$@"; }
if [ -n "$CORE" ]; then
  dev 'cp /data/banana/settings.conf /run/ra-ab.settings.bak 2>/dev/null; mkdir -p /data/retroarch/cores' 
  dev 'cat > /data/retroarch/cores/banananes_libretro.so.new && mv /data/retroarch/cores/banananes_libretro.so.new /data/retroarch/cores/banananes_libretro.so' < "$CORE" || exit 1
  export EMU_ENV='sed -i "/^core.nes *=/d" /data/banana/settings.conf; echo "core.nes = banananes" >> /data/banana/settings.conf'
fi
"$H/stress-run-nes.sh" "$D" "$H/out/stress-nes.nes" "$SEC" "$TAG" | tail -4
if [ -n "$CORE" ]; then
  dev 'if [ -f /run/ra-ab.settings.bak ]; then cp /run/ra-ab.settings.bak /data/banana/settings.conf; else sed -i "/^core.nes *=/d" /data/banana/settings.conf; fi; rm -f /run/ra-ab.settings.bak /data/retroarch/cores/banananes_libretro.so; sync'
fi

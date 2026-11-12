#!/bin/sh
# docs/showcase's videos, made again: banana-ui's headless renders of a scripted tour (make-video.py), then ffmpeg.
#   thor-tour   the AYN Thor's two screens with the menu as ROCKNIX's frontend: the home row, a GBA game's page with
#               ROCKNIX's emulators and options (Banana's beside them), Settings, Fan, lights & speed
#   thor-panel  Banana's game panel on the Thor's bottom screen while a game plays on the top (a card stands in for it)
#   rgds-tour   the RG DS: the boot animation, the home row, a DS game's page, Settings
# Needs a built ui/banana-ui, the test fixture (ui/tests/run.sh makes it) and ffmpeg. The ROCKNIX side is stood in
# for: tools/showcase/es_systems.cfg (a few systems with ROCKNIX's and Banana's emulators), ROCKNIX's es_features.cfg
# from a checkout if ROCKNIX_ES_FEATURES names one (else the tests' small one), a handheld with a fan and lights.
#   tools/showcase/render.sh [output folder: docs/showcase]
set -e
S=$(cd "$(dirname "$0")" && pwd)
R=$(cd "$S/../.." && pwd)
OUT=${1:-$R/docs/showcase}
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
[ -x "$R/ui/banana-ui" ] || { echo "build ui/banana-ui first (make -C ui)"; exit 1; }
[ -d "$R/ui/tests/out/fixture" ] || "$R/ui/tests/fixture.sh" "$R/ui/tests/out/fixture" >/dev/null
# the BananaOS systems with the GBA, Game Boy and SNES shelves run ROCKNIX's way (run-rocknix)
sed -e '/^\[gba\]/,/^\[/s#^run = .*#run = /usr/libexec/banana/run-rocknix gba %ROM% --core=mgba --emulator=retroarch --controllers=""#' \
    -e '/^\[gb\]/,/^\[/s#^run = .*#run = /usr/libexec/banana/run-rocknix gb %ROM% --core=gambatte --emulator=retroarch --controllers=""#' \
    -e '/^\[snes\]/,/^\[/s#^run = .*#run = /usr/libexec/banana/run-rocknix snes %ROM% --core=snes9x --emulator=retroarch --controllers=""#' \
    "$R/system/rootfs/etc/banana/systems.conf" > "$W/thor-systems.conf"
export WORK=$W BANANA_NOW=1767293400	# (one time on every frame: 1 January 2026, 18:50)
rx() {
    env SYSTEMS="$W/thor-systems.conf" BANANA_ROCKNIX=1 BANANA_HANDHELD="$S/handheld" \
        BANANA_ROCKNIX_OPTIONS="$R/system/rootfs/usr/libexec/banana/rocknix-options" ROCKNIX_SETTINGS="$W/system.cfg" \
        ROCKNIX_LOCK="$W/lock" ROCKNIX_ES_SYSTEMS="$S/es_systems.cfg" \
        ROCKNIX_ES_FEATURES="${ROCKNIX_ES_FEATURES:-$R/rocknix/tests/es_features.cfg}" "$@"
}
rx python3 "$S/make-video.py" thor-tour thor "$S/scenes/thor-tour.txt" "$OUT"
rx env TOPCARD="The game|RetroArch on the top screen" python3 "$S/make-video.py" thor-panel thor "$S/scenes/thor-panel.txt" "$OUT"
python3 "$S/make-video.py" rgds-tour rgds "$S/scenes/rgds-tour.txt" "$OUT"

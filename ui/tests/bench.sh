#!/bin/sh
# A reproducible measurement baseline for banana-ui, headless on this computer (no panel needed). It builds a
# synthetic library of N games across the shelves, then reports the three start-up numbers the `boot` script
# command measures -- the library scan, the menu's start-up (its constructor and the first settle) and the first
# frame's render -- plus the idle/scrolling frame cost from `bench`, at both the 640x480 and 1024x768 canvases.
#
# The numbers are a host baseline (a fast PC), not the handheld's: they tell a change from a regression, and the
# ratios between the two canvas sizes are the ones to watch. The handheld's own numbers come from `sleep_debug`
# and the QEMU harness (tools/qemu-boot.sh, resources-*.txt).
#
#   ui/tests/bench.sh [games] [runs]      games default 600, runs default 3 (the middle run is reported)
set -e
HERE=$(cd "$(dirname "$0")/.." && pwd)
GAMES=${1:-${BENCH_GAMES:-600}}
RUNS=${2:-${BENCH_RUNS:-3}}
WORK=${BENCH_DIR:-/tmp/opencode/bench}
rm -rf "$WORK"; mkdir -p "$WORK/roms" "$WORK/data"

# a library of $GAMES games, spread over the systems in the test list (folder names only; the ROMs are sparse)
i=0
mk() {  # mk <folder> <ext> <count>
    mkdir -p "$WORK/roms/$1"
    n=0
    while [ $n -lt "$3" ]; do
        head -c 1024 /dev/zero > "$WORK/roms/$1/$2 $((i + n)).$2"
        n=$((n + 1))
    done
    i=$((i + $3))
}
mk nds nds $((GAMES / 6))
mk gba gba $((GAMES / 6))
mk gbc gbc $((GAMES / 6))
mk gb gb $((GAMES / 12))
mk snes sfc $((GAMES / 12))
mk megadrive md $((GAMES / 6))
mk nes nes $((GAMES / 12))
mk psx chd $((GAMES / 12))
echo "bench: $GAMES games in $WORK/roms"

for size in 640x480 1024x768; do
    for run in $(seq "$RUNS"); do
        "$HERE/banana-ui" --headless "$WORK/$size-" --size "$size" --assets "$HERE/assets/rnds" \
            --systems "$HERE/tests/systems.conf" --roms "$WORK/roms" --data "$WORK/data" \
            --script "boot; home 0; wait 100; bench 300; bench 300 10 right" 2> "$WORK/$size-$run.log" || true
    done
    # report the middle run (the first is cold, the last is noise)
    mid=$(( (RUNS + 1) / 2 ))
    echo "--- $size (run $mid of $RUNS)"
    sed 's/^banana-ui: /  /' "$WORK/$size-$mid.log" | grep -E 'boot-metrics|bench:'
done

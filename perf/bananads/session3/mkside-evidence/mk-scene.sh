#!/bin/sh
# mk-scene.sh <bananads> <out dir> [--set k=v ...]: Mario Kart DS (USA)'s race start, reproducible, headless, from a
# FRESH save (the out dir's mkds.sav is deleted first: no user save needed). Session 3 (mkside), 2026-10-18.
#
# The input: A tapped (6 frames) every 20 frames from 120 to 1380, then held from 1400 on (gas through the countdown,
# as the device scene). With a fresh save that goes: title > "Is the nickname BananaDS correct?" YES > "BananaDS will
# appear..." YES > "Create a custom emblem?" (default) > title > Single Player > Grand Prix > 50cc > Mario > the first
# kart > Mushroom Cup > OK: Figure-8 Circuit, Grand Prix race 1.
#
# The scene (frames counted from 0, rtc.fixed below; dumps are "frame<N>.ppm" = the picture after N frames):
#   menus                  0-1290
#   loading                1290-1415  (black)
#   course intro flyover   1420-1535  ("FIGURE-8 CIRCUIT")
#   loading                1540-1675  (black)
#   grid, before countdown 1680-1775
#   countdown              1780-1965  ("3" 1780, "2" 1840, "1" 1900, "START" ~1965; the player spins out: A held)
#   race                   1965 on
#   The device's heavy window (brief: "1650-2150" with the user's save) is this script's 1680-2100.
# Dumps (headless.dump_every=60): frame1500 (intro), frame1740 (grid, before), frame1800/1860/1920 (inside),
# frame1980 (START), frame2040/2100 (race, after). Others are deleted. Audio: <out>/audio.raw (s16 stereo; cmp it).
# Known hash: frame1860.ppm (the "2") on the base build (SuperDrastic 00c34c78 / mkbase 9aa478c, render.renderer=gengis
# at scale 2). A mismatch fails the script loudly (exit 2): the input no longer reaches the scene, or the build under
# test isn't exact. Settings that change the picture (renderer, scale) make it fail too: check with KNOWN=skip.
#
# Environment: QEMU (default /home/user/perf/tools/qemu-dev when the host isn't aarch64; QEMU= runs it natively),
# FRAMES (default 2100), KNOWN (the expected md5 of frame1860.ppm; "skip" to skip the check).
# Extra --set arguments go to bananads after the script's (they win), e.g. --set headless.measure_from=1680
# --set headless.window=60 --set prof.hz=1000. Run time under qemu: ~1 min.
set -u
[ $# -ge 2 ] || { echo "usage: mk-scene.sh <bananads> <out dir> [--set k=v ...]" >&2; exit 1; }
B=$1; OUT=$2; shift 2
ROM=${ROM:-/home/user/perf3/rom/mkds.nds}
FRAMES=${FRAMES:-2100}
KNOWN=${KNOWN:-58b1524b7623ffd3737a0408f8876277}
if [ -z "${QEMU+x}" ]; then
  case $(uname -m) in aarch64) QEMU= ;; *) QEMU=/home/user/perf/tools/qemu-dev ;; esac
fi
mkdir -p "$OUT"; OUT=$(cd "$OUT" && pwd)
rm -f "$OUT"/mkds.sav "$OUT"/frame*.ppm "$OUT"/audio.raw
IN=$(python3 -c "print(','.join([f'{f}:a' for f in range(120,1400,20)]+['1400:a:1400']))")
echo "[mk-scene] $B -> $OUT, $FRAMES frames; intro 1420-1535, countdown 1780-1965 (3/2/1 at 1780/1840/1900), race 1965+"
$QEMU "$B" --frontend headless --set rtc.fixed="2026-10-18 12:00:00" --set save.dir="$OUT" \
  --set render.renderer=gengis --set render.scale=2 --set core.slice=64 --set wifi.dns=off \
  --set resume.on_start=off --set resume.on_quit=off --set headless.frames="$FRAMES" \
  --set headless.dump_dir="$OUT" --set headless.dump_every=60 --set headless.audio_dump="$OUT/audio.raw" \
  --set headless.input="$IN" "$@" "$ROM" > "$OUT/run.log" 2>&1
rc=$?
rm -f "$OUT"/mkds.sav
cd "$OUT" || exit 1
for f in frame*.ppm; do
  case $f in frame1500.ppm|frame1740.ppm|frame1800.ppm|frame1860.ppm|frame1920.ppm|frame1980.ppm|frame2040.ppm|frame2100.ppm) ;;
  *) rm -f "$f" ;; esac
done
md5sum frame*.ppm audio.raw 2>/dev/null > md5.txt
cat md5.txt
grep -E "slowest|frames [0-9]+-[0-9]+: .* of a DS|frames in" run.log
[ $rc = 0 ] || { echo "[mk-scene] FAILED: bananads exited $rc (see $OUT/run.log)" >&2; exit 1; }
got=$(md5sum frame1860.ppm 2>/dev/null | cut -d' ' -f1)
if [ "$KNOWN" != skip ] && [ "$got" != "$KNOWN" ]; then
  echo "[mk-scene] FAILED: frame1860.ppm is ${got:-missing}, not $KNOWN: the scene wasn't reached the same way" \
       "(input/menus changed) or this build/setting isn't exact. Look at frame1860.ppm (the countdown's \"2\")." >&2
  exit 2
fi
echo "[mk-scene] OK: frame1860 matches the known countdown frame"

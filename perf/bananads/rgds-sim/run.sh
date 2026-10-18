#!/bin/sh
# rgds-sim/run.sh <aarch64 bananads> <rom> <label> [--mode st|dev|raw] [--frames N1:N2] [--set k=v ...]
# Runs BananaDS under qemu-aarch64 with the rgds-sim plugin twice (N1 and N2 frames, default 100 and 300, in
# parallel) and reports frames N1..N2 per frame: each thread's modeled A55 time at 1104 and 1992 MHz, by bucket,
# with its L1I/L1D/TLB/branch events and the top functions/JIT blocks. Report: $OUT/<label>/report.txt (+ .json).
# Modes: st (default) single-threaded and deterministic: render.threaded=off, render.2d_thread=off,
#        render.gengis_threads=0 (the renderer is the build's default, gengis); dev: BananaLinux's settings
#        (render.scale=2, gengis, render.gengis_threads=2, 2d_thread at its default) with every thread reported;
#        raw: only the common settings (as BananaDS's tools/device-test.sh runs it; used for calibration).
# Env: QEMU (qemu-aarch64 with plugins), QEMU_INC (its include/qemu, to build the plugin), OUT (default
#      ./rgds-runs), SIMARGS (extra plugin args, e.g. "l3=1048576 pf=0"), PARAMS (cost-model file, default
#      rgds-sim/params.json), SYSROOT (default /usr/aarch64-linux-gnu). MELONDS_*/BANANADS_* pass through.
set -eu
B=$1; ROM=$2; L=$3; shift 3
HERE=$(cd "$(dirname "$0")" && pwd)
Q=${QEMU:-/home/user/perf/qemu-src/build/qemu-aarch64}
INC=${QEMU_INC:-/home/user/perf/qemu-src/include/qemu}
P=$HERE/build/librgdssim.so
if [ ! -f "$P" ] || [ "$HERE/rgdssim.c" -nt "$P" ]; then
  mkdir -p "$HERE/build"
  gcc -O2 -shared -fPIC -I"$INC" $(pkg-config --cflags glib-2.0) "$HERE/rgdssim.c" -o "$P.tmp.$$" && mv "$P.tmp.$$" "$P"
fi
MODE=st; N1=100; N2=300
while [ $# -gt 0 ]; do
  case $1 in
    --mode) MODE=$2; shift 2 ;;
    --frames) N1=${2%%:*}; N2=${2##*:}; shift 2 ;;
    *) break ;;
  esac
done
W=${OUT:-$PWD/rgds-runs}/$L; mkdir -p "$W"
COMMON="--frontend headless --set rtc.fixed=2026-10-11_12:00:00 --set save.dir=$W --set state.dir=$W --set wifi.dns=off --set resume.on_start=off --set resume.on_quit=off"
case $MODE in
  st) MS="--set jit=on --set render.threaded=off --set render.2d_thread=off --set render.gengis_threads=0" ;;
  dev) MS="--set jit=on --set render.scale=2 --set render.renderer=gengis --set render.gengis_threads=2" ;;
  raw) MS="" ;;
  *) echo "unknown mode $MODE"; exit 2 ;;
esac
echo "$0 $B $ROM $L --mode $MODE --frames $N1:$N2 $*" > "$W/cmd"
echo "SIMARGS=${SIMARGS:-} PARAMS=${PARAMS:-} $(env | grep -E '^(MELONDS|BANANADS)_' | tr '\n' ' ')" >> "$W/cmd"
t0=$(date +%s)
pids=""
for n in $N1 $N2; do
  MELONDS_JIT_PERFMAP=1 $Q -L "${SYSROOT:-/usr/aarch64-linux-gnu}" -plugin "$P,out=$W/f$n${SIMARGS:+,$(echo $SIMARGS | tr ' ' ',')}" \
    "$B" $COMMON $MS --set headless.frames=$n "$@" "$ROM" > "$W/log$n" 2>&1 &
  eval "pid$n=$!"; pids="$pids $!"
done
for n in $N1 $N2; do
  eval "pid=\$pid$n"
  wait $pid || echo "run $n exited with $?" >> "$W/log$n"
  mv /tmp/perf-$pid.map "$W/f$n.map" 2>/dev/null || : > "$W/f$n.map"
  rm -f /tmp/melonjit-$pid.code
done
echo "wall: $(( $(date +%s) - t0 )) s" >> "$W/cmd"
python3 "$HERE/report.py" "$W/f$N1" "$W/f$N2" $((N2 - N1)) "$L" --params "${PARAMS:-$HERE/params.json}" --json "$W/report.json" > "$W/report.txt"
cat "$W/report.txt"

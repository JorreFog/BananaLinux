#!/bin/sh
# rgds-sim/run.sh <aarch64 bananads> <rom> <label> [--mode st|dev|raw] [--frames N1:N2] [--set k=v ...]
# Runs BananaDS under qemu-aarch64 with the rgds-sim plugin and reports frames N1..N2 (default 100-300) per frame: each thread's modeled A55 time at 1104 and 1992 MHz, by bucket,
# with its L1I/L1D/TLB/branch events and the top functions/JIT blocks. Report: $OUT/<label>/report.txt (+ .json).
# Modes: st (default) single-threaded and deterministic: render.threaded=off, render.2d_thread=off,
#        render.gengis_threads=0 (the renderer is the build's default, gengis); dev: BananaLinux's settings
#        (render.scale=2, gengis, render.gengis_threads=2, 2d_thread at its default) with every thread reported;
#        raw: only the common settings (as BananaDS's tools/device-test.sh runs it; used for calibration).
# Frames: st/dev use one run with frame marks (headless.window=1's per-frame log line; the plugin runs plain qemu
#        until frame N1-10, then counts, and snapshots after frames N1 and N2); --diff (the default for raw, and for
#        builds without headless.window, e.g. older than 00c34c7) runs N1 and N2 frames separately, in parallel, and
#        subtracts.
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
MODE=st; N1=100; N2=300; DIFF=""
while [ $# -gt 0 ]; do
  case $1 in
    --mode) MODE=$2; shift 2 ;;
    --diff) DIFF=1; shift ;;
    --frames) N1=${2%%:*}; N2=${2##*:}; shift 2 ;;
    *) break ;;
  esac
done
W=${OUT:-$PWD/rgds-runs}/$L; mkdir -p "$W"
# the sysroot: a Buildroot (Bootlin GCC 14, glibc 2.41) build needs the device toolchain's (tools/build-a64-device.sh)
DEVROOT=/home/user/perf/toolchain/aarch64--glibc--stable-2025.08-1/aarch64-buildroot-linux-gnu/sysroot
if [ -z "${SYSROOT:-}" ]; then
  SYSROOT=/usr/aarch64-linux-gnu
  if [ -d $DEVROOT ] && aarch64-linux-gnu-readelf -p .comment "$B" 2>/dev/null | grep -q Buildroot; then SYSROOT=$DEVROOT; fi
fi
COMMON="--frontend headless --set rtc.fixed=2026-10-11_12:00:00 --set save.dir=$W --set state.dir=$W --set wifi.dns=off --set resume.on_start=off --set resume.on_quit=off"
case $MODE in
  st) MS="--set jit=on --set render.threaded=off --set render.2d_thread=off --set render.gengis_threads=0" ;;
  dev) MS="--set jit=on --set render.scale=2 --set render.renderer=gengis --set render.gengis_threads=2" ;;
  raw) MS="" ;;
  *) echo "unknown mode $MODE"; exit 2 ;;
esac
echo "$0 $B $ROM $L --mode $MODE --frames $N1:$N2${DIFF:+ --diff} $*" > "$W/cmd"
echo "SIMARGS=${SIMARGS:-} PARAMS=${PARAMS:-} SYSROOT=$SYSROOT $(env | grep -E '^(MELONDS|BANANADS)_' | tr '\n' ' ')" >> "$W/cmd"
[ -n "$DIFF" ] || { [ $MODE = raw ] && DIFF=1 || DIFF=0; }
# no address-space randomisation: guest mmaps land at the same host addresses each run, so cache sets are the same
NORAND=""; setarch "$(uname -m)" -R true 2>/dev/null && NORAND="setarch $(uname -m) -R"
t0=$(date +%s)
if [ $DIFF = 0 ]; then
  MELONDS_JIT_PERFMAP=1 $NORAND $Q -L "$SYSROOT" -plugin "$P,out=$W/f,marks=$N1:$N2${SIMARGS:+,$(echo $SIMARGS | tr ' ' ',')}" \
    "$B" $COMMON $MS --set headless.window=1 --set headless.frames=$N2 "$@" "$ROM" > "$W/log" 2>&1 &
  pid=$!
  wait $pid || echo "run exited with $?" >> "$W/log"
  mv /tmp/perf-$pid.map "$W/f$N1.map" 2>/dev/null || : > "$W/f$N1.map"
  cp "$W/f$N1.map" "$W/f$N2.map"; rm -f /tmp/melonjit-$pid.code
  grep -v "^\[headless\] frames [0-9]*-[0-9]*:" "$W/log" > "$W/log.short" || :
  [ -f "$W/f$N2.tsv" ] || { echo "no frame marks in the log (a build without headless.window?): use --diff"; tail -5 "$W/log"; exit 1; }
else
for n in $N1 $N2; do
  MELONDS_JIT_PERFMAP=1 $NORAND $Q -L "$SYSROOT" -plugin "$P,out=$W/f$n${SIMARGS:+,$(echo $SIMARGS | tr ' ' ',')}" \
    "$B" $COMMON $MS --set headless.frames=$n "$@" "$ROM" > "$W/log$n" 2>&1 &
  eval "pid$n=$!"
done
for n in $N1 $N2; do
  eval "pid=\$pid$n"
  wait $pid || echo "run $n exited with $?" >> "$W/log$n"
  mv /tmp/perf-$pid.map "$W/f$n.map" 2>/dev/null || : > "$W/f$n.map"
  rm -f /tmp/melonjit-$pid.code
done
fi
echo "wall: $(( $(date +%s) - t0 )) s" >> "$W/cmd"
python3 "$HERE/report.py" "$W/f$N1" "$W/f$N2" $((N2 - N1)) "$L" --params "${PARAMS:-$HERE/params.json}" --json "$W/report.json" > "$W/report.txt"
cat "$W/report.txt"

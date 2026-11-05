#!/bin/sh
# measure-dev.sh <bananads (aarch64)> <rom> <label> [--set k=v ...]: host instructions per emulated frame over frames
# 100-300 under qemu with the jitcount plugin, in BananaLinux's "device mode" (render.renderer=gengis, render.scale=2,
# render.gengis_threads=3, render.2d_thread=on, render.2d_lazy on, the frame drawn into the frontend's own buffers as
# frontend=kms does: headless.direct=on), per thread. The plugin counts per vCPU (= guest thread): vcpu 0 is the
# emulator's thread, 1-3 the Gengis render threads, 4 the 2D thread (bananads-2d). The main thread's counts are
# deterministic except for its spin-waits (WaitFence/Drain2D/WaitBin/sched_yield, reported apart) and the bins it
# draws itself (render.gengis_help=1; gx2_render_bin/f_run on vcpu 0): say which when quoting.
# Output: $OUT/<label>/report.txt (main thread, report.py buckets), threads.txt (every vcpu's total, memops, branches),
# and the raw f100.tsv / f300.tsv. Based on bananads/tools/perf/qemu-count/measure.sh.
set -eu
B=$1; ROM=$2; L=$3; shift 3
QC=/home/user/perf/base/bananads/tools/perf/qemu-count
Q=${QEMU:-/home/user/perf/qemu-src/build/qemu-aarch64}; P=${PLUGIN:-/home/user/perf/tools/libjitcount.so}
W=${OUT:-$PWD/qcount-runs}/$L; mkdir -p "$W"
DEV="--set render.renderer=gengis --set render.scale=${SCALE:-2} --set render.gengis_threads=${GXT:-3} --set render.2d_thread=${T2D:-on} --set headless.direct=on"
for n in 100 300; do
  MELONDS_JIT_PERFMAP=1 $Q -L /usr/aarch64-linux-gnu -plugin "$P,out=$W/f$n" "$B" --frontend headless --set jit=on \
    $DEV --set rtc.fixed=2026-10-11_12:00:00 \
    --set headless.frames=$n --set save.dir="$W" --set wifi.dns=off --set resume.on_start=off --set resume.on_quit=off \
    "$@" "$ROM" > "$W/log$n" 2>&1 &
  pid=$!
  wait $pid || :
  mv /tmp/perf-$pid.map "$W/f$n.map" 2>/dev/null || : > "$W/f$n.map"
  mv /tmp/melonjit-$pid.code "$W/f$n.code" 2>/dev/null || :
done
python3 "$QC/report.py" "$W/f100.tsv" "$W/f100.map" "$W/f300.tsv" "$W/f300.map" 200 0 > "$W/report.txt"
# every thread: (300-frame run - 100-frame run) / 200 frames, in thousands per frame
awk -F'\t' -v OFS='\t' 'FNR==1{f++} {k=$1; if(f==1){a[k]+=$4;m[k]+=$5;b[k]+=$6} else {A[k]+=$4;M[k]+=$5;BR[k]+=$6}}
  END{print "vcpu","kinsn/frame","kmemops/frame","kbranch/frame"; for(k in A) printf "%s\t%.1f\t%.1f\t%.1f\n", k, (A[k]-a[k])/200e3, (M[k]-m[k])/200e3, (BR[k]-b[k])/200e3}' \
  "$W/f100.tsv" "$W/f300.tsv" | sort -n > "$W/threads.txt"
echo "== $L: settings: $DEV $*"; cat "$W/threads.txt"; cat "$W/report.txt"

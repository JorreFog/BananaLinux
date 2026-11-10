#!/bin/sh
# rgds-sim/calib/device-calib.sh <bananads> <rom dir> [clocks in kHz, default "1104000 1992000"]
# Run ON THE RG DS (root, nothing else running): the workloads rgds-sim calibrates on, in its st mode's settings
# (one busy thread: render.threaded=off, render.2d_thread=off, render.gengis_threads=0), headless, at fixed clocks.
# Prints "run<TAB>mhz<TAB>fps" lines for calibrate.py (and, if perf is installed, the perf stat counters beside it).
# Then on the PC: run the same binary (same build) and ROMs with rgds-sim/run.sh --mode st under the labels below
# (OUT=<dir>), and: python3 calibrate.py <this output> <dir> --combine main --write ../params.json --version v2-device
# The fps window is frames 300-1500 (headless.measure_from), so the start-up is not in it.
set -u
B=$1; R=$2; CLOCKS=${3:-"1104000 1992000"}
CPU=/sys/devices/system/cpu/cpufreq/policy0
GOV=$(cat $CPU/scaling_governor); MIN=$(cat $CPU/scaling_min_freq); MAX=$(cat $CPU/scaling_max_freq)
trap 'echo $MIN > $CPU/scaling_min_freq; echo $MAX > $CPU/scaling_max_freq; echo $MIN > $CPU/scaling_min_freq; echo $GOV > $CPU/scaling_governor' EXIT
ST="--set jit=on --set render.threaded=off --set render.2d_thread=off --set render.gengis_threads=0"
COMMON="--frontend headless --set rtc.fixed=2026-10-11_12:00:00 --set save.dir=/tmp/rgds-calib --set wifi.dns=off --set resume.on_start=off --set resume.on_quit=off --set headless.frames=1500 --set headless.measure_from=300"
mkdir -p /tmp/rgds-calib
PERF=$(command -v perf || true)
echo "# device-calib $(date -u +%FT%TZ) $(cat /sys/firmware/devicetree/base/model 2>/dev/null | tr -d '\0') bin=$B"
for k in $CLOCKS; do
  echo performance > $CPU/scaling_governor; echo $k > $CPU/scaling_max_freq; echo $k > $CPU/scaling_min_freq; sleep 2
  for w in cpubench-cpubench-thumb cpubench-cpubench-arm cpubench-cpubench-thumb-arm7 membench-membench-arm \
           ds2dtest-ds2dtest sndbench-sndbench dsstress-L1 dsstress-L5 dsstress-L10; do
    [ -f $R/$w.nds ] || continue
    if [ -n "$PERF" ]; then
      out=$($PERF stat -e cycles,instructions,L1-icache-load-misses,L1-dcache-load-misses,branch-misses -x, -o /tmp/rgds-calib/perf.txt \
            $B $COMMON $ST $R/$w.nds 2>&1)
      pc=$(grep -v '^#' /tmp/rgds-calib/perf.txt | cut -d, -f1,3 | tr '\n' ' ')
    else out=$($B $COMMON $ST $R/$w.nds 2>&1); pc=""; fi
    fps=$(echo "$out" | sed -n 's/.*frames 300-1500: \([0-9.]*\) fps.*/\1/p' | tail -1)
    printf 'st-%s\t%d\t%s\t# %s\n' $w $((k / 1000)) "${fps:-failed}" "$pc"
    sleep 5
  done
done

#!/bin/sh
# qc.sh <outdir> <bin> [--set ...]: jitcount runs of the MK scene to 1650 and 2150 frames (concurrently), keeping maps+code
O=$1; BIN=$2; shift 2
mkdir -p $O; cd $O; O=$(pwd)
IN=$(python3 -c "print(','.join([f'{f}:a' for f in range(120,1400,20)]+['1400:a:1400']))")
for n in 1650 2150; do
  rm -rf sv$n pd$n; mkdir -p sv$n pd$n
  MELONDS_JIT_PERFMAP=1 MELONDS_JIT_PERFDIR=$O/pd$n /home/user/perf/tools/qemu-dev -plugin /home/user/perf/tools/libjitcount.so,out=$O/f$n $BIN --frontend headless --set rtc.fixed="2026-10-18 12:00:00" --set save.dir=$O/sv$n --set render.renderer=gengis --set render.scale=2 --set core.slice=64 --set wifi.dns=off --set resume.on_start=off --set resume.on_quit=off --set headless.frames=$n --set headless.input=$IN "$@" /home/user/perf3/rom/mkds.nds > log$n 2>&1 &
done
wait
for n in 1650 2150; do mv pd$n/perf-*.map f$n.map; mv pd$n/melonjit-*.code f$n.code; done
rm -f f1650.code
python3 /home/user/perf3/mk3jit/bananads/tools/perf/qemu-count/report.py f1650.tsv f1650.map f2150.tsv f2150.map 500 0 > report-v0.txt
head -3 report-v0.txt

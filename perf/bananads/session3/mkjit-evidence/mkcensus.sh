#!/bin/bash
# mkcensus.sh <bananads> <out dir> [--set ...]: mk-scene.sh with MELONDS_JIT_PERFMAP=1, then census.py on the blocks
BIN=$1; OUT=$2; shift 2
mkdir -p $OUT; OUT=$(cd $OUT && pwd); M=$OUT/.mark; touch $M; sleep 1
MELONDS_JIT_PERFMAP=1 /home/user/perf3/mk-scene.sh $BIN $OUT "$@" > $OUT/scene.out 2>&1; echo "scene exit $?" >> $OUT/scene.out
# the run's pid: the newest melonjit-*.code newer than the mark whose map isn't claimed yet
for c in $(ls -t $(find /tmp -maxdepth 1 -name "melonjit-*.code" -newer $M)); do
  P=${c#/tmp/melonjit-}; P=${P%.code}; [ -f /tmp/perf-$P.map ] || continue
  grep -q "mkds" /proc/$P/cmdline 2>/dev/null && continue   # (still running: another run's)
  mv /tmp/perf-$P.map $OUT/perf.map; mv $c $OUT/jit.code; break
done
python3 /home/user/perf/jit/census.py $OUT/jit.code $OUT/perf.map > $OUT/census.txt
gzip -f $OUT/perf.map; [ -n "${KEEP:-}" ] || rm -f $OUT/jit.code; rm -f $OUT/frame*.ppm
head -1 $OUT/census.txt

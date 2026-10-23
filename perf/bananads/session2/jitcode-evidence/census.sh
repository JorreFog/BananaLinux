#!/bin/bash
# census.sh <bananads> <rom> <frames> <outdir> [--set ...]: perf map + code dump, then census.py
BIN=$1; ROM=$2; FR=$3; OUT=$4; shift 4
mkdir -p $OUT; M=$(mktemp); sleep 1
D=$(mktemp -d)
MELONDS_JIT_PERFMAP=1 /home/user/perf/tools/qemu-dev $BIN --frontend headless --set save.dir=$D --set state.dir=$D \
  --set resume.on_start=off --set resume.on_quit=off --set wifi.dns=off --set headless.frames=$FR "$@" $ROM > $OUT/run.log 2>&1
P=$(ls -t $(find /tmp -maxdepth 1 -name "melonjit-*.code" -newer $M) | head -1); P=${P#/tmp/melonjit-}; P=${P%.code}
mv /tmp/perf-$P.map $OUT/perf.map; mv /tmp/melonjit-$P.code $OUT/jit.code; rm -rf $D $M
python3 /home/user/perf/jit/census.py $OUT/jit.code $OUT/perf.map > $OUT/census.txt; cat $OUT/census.txt

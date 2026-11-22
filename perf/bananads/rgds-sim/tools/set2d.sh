#!/bin/sh
# set2d.sh <bin> <tag> [SIMARGS]: the fixed 2D benchmark set (2D-PLAN.md), two runs at a time
B=$1; T=$2; export SIMARGS="${3:-}"
R=/home/user/BananaLinux/perf/bananads/rgds-sim/run.sh; RO=/home/user/perf/roms
export OUT=/home/user/perf/sim/s2d/$T; mkdir -p $OUT
set -- "L5-2x dev $RO/dsstress-L5.nds" "L10-2x dev $RO/dsstress-L10.nds" "ds2dtest-2x dev $RO/ds2dtest-ds2dtest.nds" \
       "membench-2x dev $RO/membench-membench.nds" "ds2dtest-1x st $RO/ds2dtest-ds2dtest.nds" "membench-1x st $RO/membench-membench.nds" \
       "cpubench-thumb-1x st $RO/cpubench-cpubench-thumb.nds" "L5-2x-st st $RO/dsstress-L5.nds --set render.scale=2" \
       "ds2dtest-2x-st st $RO/ds2dtest-ds2dtest.nds --set render.scale=2" "membench-2x-st st $RO/membench-membench.nds --set render.scale=2"
n=0
for w in "$@"; do
  set -- $w
  w1=$1; w2=$2; w3=$3; shift 3
  $R $B $w3 $w1 --mode $w2 "$@" > /dev/null 2>&1 &
  n=$((n + 1)); if [ $n -ge 2 ]; then wait; n=0; fi
done
wait
echo "set2d $T done $(date -u +%H:%M)"

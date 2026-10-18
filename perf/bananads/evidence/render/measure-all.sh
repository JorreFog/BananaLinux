#!/bin/sh
# measure-all.sh <base bananads> <mine bananads> <outdir>: the qemu-count A/B set of the render area on the device-like
# builds (QEMU=/home/user/perf/tools/qemu-dev), frames 100-300:
#  st mode (one thread; bananads/tools/perf/qemu-count/measure.sh's settings: soft 3D, no 2D thread) for the changes
#  on the emulator's thread: membench and ds2dtest (render.2d_comp_unroll), geomtest and dsstress L10 (render.geom_sort);
#  device mode (measure-dev.sh: gengis 2x, 3 threads, 2D thread, headless.direct) per thread on dsstress L5 and L10
#  (render.2d_quarters, render.2d_emit_zip on the 2D thread; render.3d_pipeline on the emulator's thread);
#  1x with the 2D thread on dsstress L5 (render.3d_share at 1x).
# Two runs at a time. Output: <outdir>/<label>/report.txt (+ threads.txt in device mode) and <outdir>/summary.txt.
set -u
BASE=$1; MINE=$2; OUT=$3; mkdir -p "$OUT"
export QEMU=/home/user/perf/tools/qemu-dev
QC=/home/user/perf/base/bananads/tools/perf/qemu-count/measure.sh
DEV=/home/user/BananaLinux/perf/bananads/evidence/render/measure-dev.sh
R=/home/user/perf/roms
st() { OUT=$OUT sh $QC "$@" > /dev/null 2>&1; }
dev() { OUT=$OUT sh $DEV "$@" > /dev/null 2>&1; }
# st mode
st $BASE $R/membench-membench.nds st-membench-base &
st $MINE $R/membench-membench.nds st-membench-mine & wait
st $MINE $R/membench-membench.nds st-membench-unroll-off --set render.2d_comp_unroll=off &
st $BASE $R/ds2dtest-ds2dtest.nds st-ds2dtest-base & wait
st $MINE $R/ds2dtest-ds2dtest.nds st-ds2dtest-mine &
st $MINE $R/ds2dtest-ds2dtest.nds st-ds2dtest-unroll-off --set render.2d_comp_unroll=off & wait
st $BASE $R/geomtest-geomtest.nds st-geomtest-base &
st $MINE $R/geomtest-geomtest.nds st-geomtest-mine & wait
st $MINE $R/geomtest-geomtest.nds st-geomtest-sort-off --set render.geom_sort=off &
st $BASE $R/dsstress-L10.nds st-L10-base & wait
st $MINE $R/dsstress-L10.nds st-L10-mine &
st $MINE $R/dsstress-L10.nds st-L10-sort-off --set render.geom_sort=off & wait
# device mode, per thread
dev $BASE $R/dsstress-L5.nds dev-L5-base &
dev $MINE $R/dsstress-L5.nds dev-L5-mine & wait
dev $MINE $R/dsstress-L5.nds dev-L5-quarters-off --set render.2d_quarters=off &
dev $MINE $R/dsstress-L5.nds dev-L5-zip-off --set render.2d_emit_zip=off & wait
dev $MINE $R/dsstress-L5.nds dev-L5-pipeline-off --set render.3d_pipeline=off &
dev $MINE $R/dsstress-L5.nds dev-L5-alloff --set render.2d_quarters=off --set render.2d_emit_zip=off --set render.3d_pipeline=off --set render.2d_comp_unroll=off --set render.geom_sort=off & wait
dev $BASE $R/dsstress-L10.nds dev-L10-base &
dev $MINE $R/dsstress-L10.nds dev-L10-mine & wait
# 1x with the 2D thread
SCALE=1 GXT=2 dev $MINE $R/dsstress-L5.nds dev1x-L5-share-off --set render.3d_share=off &
SCALE=1 GXT=2 dev $MINE $R/dsstress-L5.nds dev1x-L5-mine & wait
SCALE=1 GXT=2 dev $BASE $R/dsstress-L5.nds dev1x-L5-base & wait
{
  echo "# measure-all $(date -u +%FT%TZ) base=$BASE mine=$MINE QEMU=$QEMU (frames 100-300, k instructions/frame)"
  for d in $OUT/st-* $OUT/dev-* $OUT/dev1x-*; do
    [ -f $d/report.txt ] || continue
    printf "%-28s %s" "$(basename $d)" "$(head -1 $d/report.txt)"
    [ -f $d/threads.txt ] && printf " | threads: %s" "$(tail -n +2 $d/threads.txt | awk '{printf "%s:%s ", $1, $2}')"
    echo
  done
} > $OUT/summary.txt
cat $OUT/summary.txt

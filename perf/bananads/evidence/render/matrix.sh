#!/bin/sh
# matrix.sh <label> <binA> <binB> "<A-only --set>" "<B-only --set>" [rom...]: abcheck.sh over the exactness matrix of
# the render area: render.scale 1 and 2, Gengis with 0/1/2/3 threads, the 2D thread off and on, every 10th frame of
# 300 (the ROMs: dsstress L1/L5/L10/ramp ARM and Thumb L5/L10, dsswap, ds2dtest, membench, geomtest unless given),
# plus a savestate reload at frame 150 in the device configuration. One abcheck (two qemu runs) at a time.
# Prints one line per configuration and a FAILS count at the end; the per-run dumps stay under $OUT/<label>-*/.
set -u
L=$1; A=$2; B=$3; AO=$4; BO=$5; shift 5
R=/home/user/perf/roms
ROMS=${*:-"$R/dsstress-L1.nds $R/dsstress-L5.nds $R/dsstress-L10.nds $R/dsstress-ramp.nds $R/thumb-dsstress-L5.nds $R/thumb-dsstress-L10.nds $R/dsswap.nds $R/ds2dtest-ds2dtest.nds $R/membench-membench.nds $R/geomtest-geomtest.nds"}
AB=/home/user/BananaLinux/perf/bananads/evidence/render/abcheck.sh
fails=0; n=0
cfg() { # name common-settings
  name=$1; shift
  for rom in $ROMS; do
    r=$(basename $rom .nds)
    n=$((n+1))
    out=$(sh $AB "$L-$name-$r" "$A" "$B" "$rom" 300 10 --set render.renderer=gengis "$@" -- $AO -- $BO 2>&1 | tail -1)
    case "$out" in *": SAME"*) ;; *) fails=$((fails+1));; esac
    echo "$out"
  done
}
cfg dev2x    --set render.scale=2 --set render.gengis_threads=3 --set render.2d_thread=on --set headless.direct=on
cfg dev2x-reload --set render.scale=2 --set render.gengis_threads=3 --set render.2d_thread=on --set headless.direct=on --set headless.reload_at=150
cfg 2x-t2    --set render.scale=2 --set render.gengis_threads=2 --set render.2d_thread=on
cfg 2x-t1    --set render.scale=2 --set render.gengis_threads=1 --set render.2d_thread=on
cfg 2x-t0    --set render.scale=2 --set render.gengis_threads=0 --set render.2d_thread=on
cfg 2x-no2d  --set render.scale=2 --set render.gengis_threads=3 --set render.2d_thread=off
cfg 1x-t3    --set render.scale=1 --set render.gengis_threads=3 --set render.2d_thread=off
cfg 1x-t2-2d --set render.scale=1 --set render.gengis_threads=2 --set render.2d_thread=on --set headless.direct=on
cfg 1x-t1    --set render.scale=1 --set render.gengis_threads=1 --set render.2d_thread=on
cfg 1x-t0    --set render.scale=1 --set render.gengis_threads=0 --set render.2d_thread=off
echo "$L: $n configurations, FAILS=$fails"

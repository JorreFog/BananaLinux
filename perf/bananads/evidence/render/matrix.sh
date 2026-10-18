#!/bin/sh
# matrix.sh <label> <binA> <binB> "<A-only --set>" "<B-only --set>": abcheck.sh over the exactness matrix of the render
# area: render.scale 1 and 2, Gengis with 0/1/2/3 threads, the 2D thread off and on, every 10th frame of 300, plus a
# savestate reload at frame 150 in the device configuration. The device configuration (2x, 3 Gengis threads, 2D
# thread, headless.direct as frontend=kms) runs every ROM (dsstress L1/L5/L10/ramp ARM, Thumb L5/L10, dsswap,
# ds2dtest, membench, geomtest); the others a subset (CONFIGS=... limits the configurations). One abcheck (two qemu
# runs) at a time. Prints one line per configuration and a FAILS count at the end; the dumps stay under $OUT/<label>-*/.
set -u
L=$1; A=$2; B=$3; AO=$4; BO=$5
R=/home/user/perf/roms
ALL="$R/dsstress-L1.nds $R/dsstress-L5.nds $R/dsstress-L10.nds $R/dsstress-ramp.nds $R/thumb-dsstress-L5.nds $R/thumb-dsstress-L10.nds $R/dsswap.nds $R/ds2dtest-ds2dtest.nds $R/membench-membench.nds $R/geomtest-geomtest.nds"
FEW="$R/dsstress-L5.nds $R/ds2dtest-ds2dtest.nds $R/geomtest-geomtest.nds"
MID="$R/dsstress-L5.nds $R/dsstress-L10.nds $R/ds2dtest-ds2dtest.nds $R/membench-membench.nds $R/geomtest-geomtest.nds"
AB=/home/user/BananaLinux/perf/bananads/evidence/render/abcheck.sh
CONFIGS=${CONFIGS:-"dev2x dev2x-reload 2x-t2 2x-t1 2x-t0 2x-no2d 1x-t3 1x-t2-2d 1x-t1 1x-t0"}
fails=0; n=0
cfg() { # name roms common-settings...
  name=$1; roms=$2; shift 2
  case " $CONFIGS " in *" $name "*) ;; *) return;; esac
  for rom in $roms; do
    r=$(basename $rom .nds)
    n=$((n+1))
    out=$(sh $AB "$L-$name-$r" "$A" "$B" "$rom" 300 10 --set render.renderer=gengis "$@" -- $AO -- $BO 2>&1 | tail -1)
    case "$out" in *": SAME"*) ;; *) fails=$((fails+1));; esac
    echo "$out"
  done
}
cfg dev2x        "$ALL" --set render.scale=2 --set render.gengis_threads=3 --set render.2d_thread=on --set headless.direct=on
cfg dev2x-reload "$MID" --set render.scale=2 --set render.gengis_threads=3 --set render.2d_thread=on --set headless.direct=on --set headless.reload_at=150
cfg 2x-t2        "$FEW" --set render.scale=2 --set render.gengis_threads=2 --set render.2d_thread=on
cfg 2x-t1        "$FEW" --set render.scale=2 --set render.gengis_threads=1 --set render.2d_thread=on
cfg 2x-t0        "$FEW" --set render.scale=2 --set render.gengis_threads=0 --set render.2d_thread=on
cfg 2x-no2d      "$FEW" --set render.scale=2 --set render.gengis_threads=3 --set render.2d_thread=off
cfg 1x-t3        "$FEW" --set render.scale=1 --set render.gengis_threads=3 --set render.2d_thread=off
cfg 1x-t2-2d     "$MID" --set render.scale=1 --set render.gengis_threads=2 --set render.2d_thread=on --set headless.direct=on
cfg 1x-t1        "$FEW" --set render.scale=1 --set render.gengis_threads=1 --set render.2d_thread=on
cfg 1x-t0        "$FEW" --set render.scale=1 --set render.gengis_threads=0 --set render.2d_thread=off
echo "$L: $n configurations, FAILS=$fails"

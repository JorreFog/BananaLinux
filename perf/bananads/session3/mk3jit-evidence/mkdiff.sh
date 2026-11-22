#!/bin/sh
# mkdiff.sh <difftest bin> <frames> <from> "<A k=v ...>" "<B k=v ...>" [extra difftest flags]: MK scene input, main RAM
# (+ flags, e.g. --regs --timing) every frame from <from>
DT=$1; N=$2; F=$3; SA=$4; SB=$5; shift 5
IN=$(python3 -c "print(','.join([f'{f}:a' for f in range(120,1400,20)]+['1400:a:1400']))")
D=$(mktemp -d /home/user/perf3/mk3jit-w/runs/dt.XXXX)
args="--a jit=on --b jit=on --a rtc.fixed=2026-10-18_12:00:00 --b rtc.fixed=2026-10-18_12:00:00 --a save.dir=$D/a --b save.dir=$D/b --a render.renderer=gengis --b render.renderer=gengis --a render.scale=2 --b render.scale=2 --a core.slice=64 --b core.slice=64"
mkdir -p $D/a $D/b
for kv in $SA; do args="$args --a $kv"; done
for kv in $SB; do args="$args --b $kv"; done
/home/user/perf/tools/qemu-dev $DT --frames $N --every --from $F --fresh-save --input $IN "$@" $args /home/user/perf3/rom/mkds.nds
rc=$?; rm -rf $D; exit $rc

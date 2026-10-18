#!/bin/bash
# rgds-sim/tools/exact.sh <binA> <binB> <outdir> "<A settings>" "<B settings>" [quick]
# The verifier's exactness check, two binaries (base vs a branch, or one binary twice): pictures every 50 frames to
# 300 (rtc.fixed) and, for the sound ROMs, the raw audio dump, md5-compared, on the test ROMs; with one binary
# (binA = binB) also bananads-difftest --every --frames 300 (main RAM every frame) on the CPU ROMs.
# Settings: space-separated K=V; ENV:NAME=V sets an environment variable for that side. Picks the sysroot like
# run.sh (Buildroot builds: the device toolchain's). EXTRA_ROMS="<abs paths>" adds ROMs to both lists. Prints one
# line per check and FAILS=n.
set -u
BA=$1; BB=$2; OUT=$3; SA=$4; SB=$5; QUICK=${6:-}
Q=${QEMU:-/home/user/perf/qemu-src/build/qemu-aarch64}; R=${ROMS:-/home/user/perf/roms}
DEVROOT=/home/user/perf/toolchain/aarch64--glibc--stable-2025.08-1/aarch64-buildroot-linux-gnu/sysroot
sysroot() { if [ -d $DEVROOT ] && aarch64-linux-gnu-readelf -p .comment "$1" 2>/dev/null | grep -q Buildroot; then echo $DEVROOT; else echo /usr/aarch64-linux-gnu; fi; }
mkdir -p "$OUT"; OUT=$(cd "$OUT" && pwd)
COMMON="--frontend headless --set rtc.fixed=2026-10-11_12:00:00 --set wifi.dns=off --set resume.on_start=off --set resume.on_quit=off --set jit=on"
split() { SETS=""; ENVS=""; for kv in $1; do case $kv in ENV:*) ENVS="$ENVS ${kv#ENV:}";; *) SETS="$SETS --set $kv";; esac; done; }
FAILS=0
pics() {
  local rom=$1 n=$(basename $1 .nds) side s b
  for side in A B; do
    if [ $side = A ]; then s=$SA; b=$BA; else s=$SB; b=$BB; fi
    split "$s"
    local d=$OUT/pics/$n.$side; rm -rf $d; mkdir -p $d/save
    env $ENVS $Q -L $(sysroot $b) $b $COMMON --set headless.frames=300 --set headless.dump_dir=$d \
      --set headless.dump_every=50 --set save.dir=$d/save --set headless.audio_dump=$d/audio.raw $SETS $(case $rom in /*) echo $rom;; *) echo $R/$rom;; esac) > $d/log 2>&1 &
  done
  wait
  local a b2 cnt
  a=$(cd $OUT/pics/$n.A && md5sum *.ppm audio.raw 2>/dev/null | sort -k2)
  b2=$(cd $OUT/pics/$n.B && md5sum *.ppm audio.raw 2>/dev/null | sort -k2)
  cnt=$(echo "$a" | grep -c .)
  if [ -n "$a" ] && [ "$a" = "$b2" ] && [ "$cnt" -ge 7 ]; then echo "PICS+AUDIO ok   $n ($cnt files identical)"
  else echo "PICS+AUDIO FAIL $n (A $cnt files)"; diff <(echo "$a") <(echo "$b2") | head -4; FAILS=$((FAILS+1)); fi
}
dtest() {
  local rom=$1 n=$(basename $1 .nds) da="" db=""
  split "$SA"; local ea=$ENVS; for kv in $SA; do case $kv in ENV:*) ;; *) da="$da --a $kv";; esac; done
  split "$SB"; local eb=$ENVS; for kv in $SB; do case $kv in ENV:*) ;; *) db="$db --b $kv";; esac; done
  [ "$ea" = "$eb" ] || { echo "DIFF SKIP $n (A and B differ by environment only)"; return; }
  mkdir -p $OUT/diff
  env $ea $Q -L $(sysroot $BA) $(dirname $BA)/bananads-difftest --every --frames 300 --a jit=on --b jit=on $da $db $(case $rom in /*) echo $rom;; *) echo $R/$rom;; esac) > $OUT/diff/$n.log 2>&1
  if [ $? = 0 ] && grep -q "^SAME" $OUT/diff/$n.log; then echo "DIFF ok   $n ($(grep ^SAME $OUT/diff/$n.log | head -1))"
  else echo "DIFF FAIL $n ($(tail -2 $OUT/diff/$n.log | tr '\n' ' '))"; FAILS=$((FAILS+1)); fi
}
if [ "$QUICK" = quick ]; then
  PROMS="cpubench-cpubench-thumb.nds dsstress-L5.nds dmasnd-dmasnd.nds"; DROMS="cpubench-cpubench-thumb.nds"
else
  PROMS="cpubench-cpubench-thumb.nds cpubench-cpubench-arm.nds cpubench-cpubench-thumb-arm7.nds dsstress-L1.nds dsstress-L5.nds thumb-dsstress-L5.nds arm7-thumb-dsstress-L5.nds dsstress-L10.nds iorom-dsstress-L5.nds ipcrom-ipctest.nds dmasnd-dmasnd.nds sndbench-sndbench.nds ds2dtest-ds2dtest.nds geomtest-geomtest.nds jitloops-jitloops-thumb.nds jitloops-jitloops-arm.nds membench-membench.nds"
  DROMS="cpubench-cpubench-thumb.nds cpubench-cpubench-thumb-arm7.nds membench-membench.nds jitloops-jitloops-thumb.nds jitloops-jitloops-arm.nds iorom-dsstress-L5.nds"
fi
PROMS="$PROMS ${EXTRA_ROMS:-}"; DROMS="$DROMS ${EXTRA_ROMS:-}"
echo "# exact.sh $(date -u +%FT%TZ) A=$BA [$SA] B=$BB [$SB] $QUICK ${EXTRA_ROMS:+extra: $EXTRA_ROMS}"
for r in $PROMS; do pics $r; done
if [ "$BA" = "$BB" ] && [ -x "$(dirname $BA)/bananads-difftest" ]; then for r in $DROMS; do dtest $r; done
else echo "DIFF SKIP (two binaries: difftest compares settings within one binary)"; fi
echo "FAILS=$FAILS"

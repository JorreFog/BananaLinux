#!/bin/bash
# exact-suite.sh <build/b dir> <out dir> "<A settings>" "<B settings>" [quick]
#   A/B settings: space-separated K=V (BananaDS settings, e.g. "jit.hugepages=off"); an entry ENV:NAME=V sets an
#   environment variable for that side instead (e.g. "ENV:MELONDS_JIT_BUDGET=0").
# The jit agent's exactness suite (2026-10-18), toggle off (A) against on (B), aarch64 binaries under qemu:
#  1. pictures every 50 frames to 300 (rtc.fixed) identical: cpubench thumb/arm/thumb-arm7, stress L5 arm/thumb/
#     arm7-thumb, L10, iorom L5, ipcrom, dmasnd, jitloops thumb/arm/thumb-arm7, membench (+ the audio dump of dmasnd)
#  2. bananads-difftest --every --frames 300, main RAM A vs B: cpubench thumb, cpubench thumb-arm7 (busy ARM7),
#     membench, jitloops thumb/arm/thumb-arm7
#  3. bananads-tests
# "quick" as 5th argument: pictures and difftest on cpubench thumb + jitloops thumb only.
# Prints one line per check and "FAILS=n" at the end.
set -u
B=$(cd "$1" && pwd); OUT=$2; SA=$3; SB=$4; QUICK=${5:-}
Q=${QEMU:-/home/user/perf/qemu-src/build/qemu-aarch64}; R=/home/user/perf/roms
# (QEMU=/home/user/perf/tools/qemu-dev for build-a64-device.sh binaries: it drops the -L below for its own sysroot)
mkdir -p "$OUT"; OUT=$(cd "$OUT" && pwd)
COMMON="--set rtc.fixed=2026-10-11_12:00:00 --set wifi.dns=off --set resume.on_start=off --set resume.on_quit=off --set jit=on"
split() {  # split "<settings>" -> SETS (--set k=v ...) and ENVS (k=v ...)
  SETS=""; ENVS=""
  for kv in $1; do case $kv in ENV:*) ENVS="$ENVS ${kv#ENV:}";; *) SETS="$SETS --set $kv";; esac; done
}
dsplit() {  # for difftest: --a/--b k=v; env vars apply to both runs of the process (so only usable when A=B env)
  DS=""; for kv in $2; do case $kv in ENV:*) ;; *) DS="$DS --$1 $kv";; esac; done
}
FAILS=0
pics() {  # pics <rom>
  local rom=$1 n=$(basename $1 .nds) side s
  for side in A B; do
    if [ $side = A ]; then s=$SA; else s=$SB; fi
    split "$s"
    local d=$OUT/pics/$n.$side; rm -rf $d; mkdir -p $d/save
    local extra=""; case $n in dmasnd*|sndbench*) extra="--set headless.audio_dump=$d/audio.raw";; esac
    env $ENVS $Q -L /usr/aarch64-linux-gnu $B/bananads --frontend headless $COMMON --set headless.frames=300 \
      --set headless.dump_dir=$d --set headless.dump_every=50 --set save.dir=$d/save $extra $SETS $R/$rom > $d/log 2>&1 &
  done
  wait
  local a b
  a=$(cd $OUT/pics/$n.A && md5sum *.ppm *.png audio.raw 2>/dev/null | sort -k2)
  b=$(cd $OUT/pics/$n.B && md5sum *.ppm *.png audio.raw 2>/dev/null | sort -k2)
  local cnt=$(echo "$a" | grep -c . )
  if [ -n "$a" ] && [ "$a" = "$b" ] && [ "$cnt" -ge 6 ]; then echo "PICS ok   $n ($cnt files identical)"; else echo "PICS FAIL $n (A $cnt files; diff below)"; diff <(echo "$a") <(echo "$b") | head -5; FAILS=$((FAILS+1)); fi
}
dtest() {  # dtest <rom>
  local rom=$1 n=$(basename $1 .nds)
  split "$SA"; local ea=$ENVS; split "$SB"; local eb=$ENVS
  if [ "$ea" != "$eb" ]; then
    # env toggles: the two runs in one process can't differ; run difftest A-vs-A under each env and compare
    # the per-frame hashes isn't possible either, so use --a/--b settings only, and say so
    echo "DIFF SKIP $n (A and B differ only by environment: give the toggle a setting)"; return
  fi
  dsplit a "$SA"; local da=$DS; dsplit b "$SB"; local db=$DS
  mkdir -p $OUT/diff
  env $ea $Q -L /usr/aarch64-linux-gnu $B/bananads-difftest --every --frames 300 --a jit=on --b jit=on $da $db $R/$rom > $OUT/diff/$n.log 2>&1
  local rc=$?
  if [ $rc = 0 ]; then echo "DIFF ok   $n ($(tail -1 $OUT/diff/$n.log))"; else echo "DIFF FAIL $n rc=$rc ($(tail -2 $OUT/diff/$n.log | tr '\n' ' '))"; FAILS=$((FAILS+1)); fi
}
if [ "$QUICK" = quick ]; then
  PROMS="cpubench-cpubench-thumb.nds jitloops-jitloops-thumb.nds"; DROMS="cpubench-cpubench-thumb.nds jitloops-jitloops-thumb.nds"
else
  PROMS="cpubench-cpubench-thumb.nds cpubench-cpubench-arm.nds cpubench-cpubench-thumb-arm7.nds dsstress-L5.nds thumb-dsstress-L5.nds arm7-thumb-dsstress-L5.nds dsstress-L10.nds iorom-dsstress-L5.nds ipcrom-ipctest.nds dmasnd-dmasnd.nds jitloops-jitloops-thumb.nds jitloops-jitloops-arm.nds jitloops-jitloops-thumb-arm7.nds membench-membench.nds"
  DROMS="cpubench-cpubench-thumb.nds cpubench-cpubench-thumb-arm7.nds membench-membench.nds jitloops-jitloops-thumb.nds jitloops-jitloops-arm.nds jitloops-jitloops-thumb-arm7.nds"
fi
echo "# exact-suite $(date -u +%FT%TZ) bin=$B A='$SA' B='$SB' $QUICK"
for r in $PROMS; do pics $r; done
for r in $DROMS; do dtest $r; done
if [ "$QUICK" != quick ]; then
  split "$SB"
  env $ENVS $Q -L /usr/aarch64-linux-gnu $B/bananads-tests > $OUT/tests.log 2>&1 && echo "TESTS ok ($(tail -1 $OUT/tests.log))" || { echo "TESTS FAIL ($(tail -2 $OUT/tests.log | tr '\n' ' '))"; FAILS=$((FAILS+1)); }
fi
echo "FAILS=$FAILS"

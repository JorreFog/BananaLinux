#!/bin/sh
# sabchain.sh <variants...>: the flagstub sabotages (MELONDS_JIT_SABFS=n) on the MK scene: frames+audio vs base, then
# main RAM (difftest, flagstub off (A) vs on (B), every frame from 1600) when the frames don't catch it
W=/home/user/perf3/mkjit-work; SB=$W/bsab/b; BASE=/home/user/perf3/mkside-work/s-base/md5.txt
for n in "$@"; do
  d=$W/sab$n; MELONDS_JIT_SABFS=$n KNOWN=skip /home/user/perf3/mk-scene.sh $SB/bananads $d > $d.out 2>&1
  nd=$(diff $d/md5.txt $BASE | grep -c '^<')
  echo "SAB $n: scene exit/frames: $(grep -E 'FAILED|frames in' $d.out $d/run.log | tail -n 1 | cut -c1-80); md5 lines differing from base: $nd ($(diff $d/md5.txt $BASE | grep '^<' | awk '{print $3}' | tr '\n' ' '))"
  rm -f $d/frame*.ppm
  if [ "$nd" = 0 ]; then
    r=$(MELONDS_JIT_SABFS=$n $W/bin/mkdiff.sh $SB/bananads-difftest 2100 1600 jit.flagstub=off jit.flagstub=on 2>&1 | tail -n 2 | tr '\n' ' ')
    echo "SAB $n: difftest: $r"
  fi
done
echo SABCHAIN-DONE

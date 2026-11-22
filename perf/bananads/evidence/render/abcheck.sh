#!/bin/sh
# abcheck.sh <label> <binA> <binB> <rom> <frames> <every> <common --set...> -- <A-only --set...> -- <B-only --set...>
# Runs A and B headless under qemu-aarch64 with rtc.fixed, dumps every <every>th frame of <frames> and the audio, and
# compares the dumps and the logs' fps line count. Prints SAME or DIFF per file; exit 1 on any difference.
# Pass "--" sections even when empty. Output under $OUT/<label>/{a,b}.
set -u
L=$1; A=$2; B=$3; ROM=$4; N=$5; E=$6; shift 6
COMMON=""; while [ $# -gt 0 ] && [ "$1" != "--" ]; do COMMON="$COMMON $1"; shift; done; shift
AO=""; while [ $# -gt 0 ] && [ "$1" != "--" ]; do AO="$AO $1"; shift; done; [ $# -gt 0 ] && shift
BO="$*"
Q=${QEMU:-/home/user/perf/qemu-src/build/qemu-aarch64}
W=${OUT:-$PWD/ab-runs}/$L; rm -rf "$W"; mkdir -p "$W/a" "$W/b"
run() { # dir bin extra
  $Q -L /usr/aarch64-linux-gnu "$2" --frontend headless --set jit=on --set rtc.fixed=2026-10-11_12:00:00 \
    --set headless.frames=$N --set headless.dump_dir="$1" --set headless.dump_every=$E --set headless.audio_dump="$1/audio.raw" \
    --set save.dir="$1" --set wifi.dns=off --set resume.on_start=off --set resume.on_quit=off $COMMON $3 "$ROM" > "$1/log" 2>&1
}
run "$W/a" "$A" "$AO" & run "$W/b" "$B" "$BO" & wait
fail=0
for f in $(cd "$W/a" && ls frame*.ppm audio.raw 2>/dev/null | sort -V); do
  if cmp -s "$W/a/$f" "$W/b/$f"; then :; else echo "DIFF $f"; fail=1; fi
done
na=$(ls "$W/a"/frame*.ppm 2>/dev/null | wc -l); nb=$(ls "$W/b"/frame*.ppm 2>/dev/null | wc -l)
[ "$na" = "$nb" ] && [ "$na" -gt 0 ] || { echo "DUMPS a=$na b=$nb"; fail=1; }
grep -q "the DS stopped" "$W/a/log" && grep -q "the DS stopped" "$W/b/log" || { echo "a run did not finish"; fail=1; }
echo "$L: $( [ $fail = 0 ] && echo SAME || echo DIFF ) ($na dumps + audio; $(basename $ROM) $N/$E; common:$COMMON | A:$AO | B:$BO)"
exit $fail

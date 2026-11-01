#!/bin/bash
# ab-nes.sh <plus|rgds> <seconds> <pin_khz|0> <rom> core:tag [core:tag ...]  -- run under locked.sh (hold < 20 min).
# Runs every core in BananaFront (devrun-nes.sh) one after the other, then prints a table of the run ms / CPU % from stats.txt.
# Repeat the list (a:A b:B a:A2 b:B2) for A/B/A/B. Needs ROM path (absolute).
set -u
DEV=$1 SEC=$2 PIN=$3 ROM=$4; shift 4
H=$(dirname "$(readlink -f "$0")")
for ct in "$@"; do
  core=${ct%%:*}; tag=${ct##*:}
  if [ "$PIN" != 0 ]; then export PIN_KHZ=$PIN; else unset PIN_KHZ; fi
  case $tag in *direct*) export BF_ENV="BANANAFRONT_DIRECT=1";; *) unset BF_ENV;; esac
  "$H/devrun-nes.sh" "$DEV" nes "$core" "$ROM" "$SEC" "$tag" >/dev/null 2>&1 || echo "run $tag failed" >&2
done
for ct in "$@"; do
  tag=${ct##*:}
  d=$(ls -dt ~/bf-runs/$(basename "${ROM%.*}")/*-$DEV-$tag 2>/dev/null | head -1)
  [ -n "$d" ] || { echo "$tag: no run"; continue; }
  grep ' stats ' "$d/stats.txt" | tail -n +2 | awk -v t="$tag" '{ for (i = 1; i <= NF; i++) { if ($i == "run") { split($(i+1), a, "/"); r += a[1]; p99 += a[3] } if ($i == "cpu") c += $(i+1) } n++ }
     END { printf "%-14s run avg %.2f ms  p99 %.2f ms  cpu %.1f %%  (%d windows)\n", t, r/n, p99/n, c/n, n }'
done

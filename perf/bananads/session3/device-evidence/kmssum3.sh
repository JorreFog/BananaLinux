#!/bin/sh
# kmssum3.sh <tag>...: per run, each 60-frame window's shown fps, windows under 59, repeats (refreshes without a new
# frame) and drops, the clocks cpugov went to, and the audio stats' short chunks. Tags are /data/kmsmk/<tag>.
for t in "$@"; do L=/data/kmsmk/$t/bananads.log; [ -f $L ] || { echo "$t: no log"; continue; }
  awk -v t=$t '/^\[kms\] 60 frames shown/ {v=$8; gsub(/[^0-9.]/,"",v); n++; f[n]=v; if (v<59) {bad++; lost+=60-v}
      for (i=1;i<=NF;i++) { if ($i=="refreshes" && $(i-1) ~ /^[0-9]+$/) rep+=$(i-1); if ($i=="dropped" && $(i-1) ~ /^[0-9]+$/) drp+=$(i-1) } }
    /^\[cpugov\] [0-9]+ -> / {clk=clk" "$4}
    /^\[alsa\] 10 s:/ {sc+=$4}
    END {printf "%-12s windows %d, under 59: %d (frames lost ~%.0f), repeats %d, dropped %d, short audio chunks %d, clocks:%s\n", t, n, bad, lost, rep, drp, sc, clk; s=""; for(i=1;i<=n;i++) s=s sprintf("%.0f ", f[i]); print "  " s}' $L
done

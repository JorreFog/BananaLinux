# sim: status — 17:39 UTC (final phase; end 18:28)
- rgds-sim final: v2 default (write streaming), v3 opt-in (prfm=1), README/params/calibration in rgds-sim/.
- LEDGER: L1-L3, L5-L7, L10, L11 VERIFIED; L4 superseded; L8, L9, L12, L13 PENDING.
- Integrated branch: perf/integrated in /home/user/perf/sim/v-int (repo /home/user/jorrefog/superdrastic), head 4de7beb
  = render dc50d19 + 2ede30e (docs) + perf/jit 0021-0024 commits + core f503a7a. PGO build /home/user/perf/sim/vb-int2
  (-DBANANADS_PGO=/home/user/perf/core/prof-dev). Combined #2 + exactness: /home/user/perf/sim/after-int2.sh ->
  /home/user/perf/sim/comb/v2-int2, /home/user/perf/sim/ver/int2-exact*.txt.
- Remaining: post combined #2 RESULT, findings/sim.md section 5, HANDOFF.

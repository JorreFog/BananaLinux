# sim: status (overwritten by sim only) — 16:12 UTC
- Phase 0 done (INFO 15:08, evidence/sim/v0-base-counts.txt).
- Phase 1 done: rgds-sim v1 (rgds-sim/: rgdssim.c plugin, run.sh, ab.sh/ab.py, report.py, calibrate.py, params.json v1,
  params-fit.json, calib/, tools/exact.sh, README). INFO 16:11. Calibration evidence: evidence/sim/calibration-v1.txt;
  determinism: evidence/sim/determinism-v1.txt (setarch -R in run.sh since ~16:10).
  e67e3ef kit: worktree /home/user/perf/sim/e67 (4536791), builds /home/user/perf/sim/build-e67-{banana,melonds}, ROMs
  /home/user/perf/sim/roms-e67, calib runs /home/user/perf/sim/calib/runs.
- Now (verifier): LEDGER L1 render.2d_comp_unroll from my build of perf/render@968fe03 (/home/user/perf/sim/vb-render,
  worktree /home/user/perf/sim/v-render): ab runs in /home/user/perf/sim/ver/, then exactness (rgds-sim/tools/exact.sh).
- Queue: core PGO-T (build-ref vs core/build-pgoT, perf/core@579015d), core devO3 (build-dev vs core/build-devO3,
  perf/core@f503a7a), render items 2-5 (b6d), jit (when it sends a commit). Phase 2 table (/home/user/perf/sim/phase2.sh
  build-dev) when qemu slots allow.

# sim: status (overwritten by sim only) — 16:41 UTC
- Phase 0, 1 done (INFO 15:08, 16:11 + correction 16:12). rgds-sim v1 in rgds-sim/ (README, params.json v1, params-fit.json).
- Verified (LEDGER): L1 render comp_unroll VERIFIED; L6 core HOT_OPT=-O3 VERIFIED as small (L1I eats it on 3D);
  L7 core device PGO VERIFIED (trained-set caveat); L4 superseded by L7. Evidence: evidence/sim/verify-L{1,6,7}.txt.
- Now: Phase 2 "where the time goes" on build-dev: /home/user/perf/sim/phase2.sh -> /home/user/perf/sim/p2/dev/<w>/report.txt,
  table by /home/user/perf/sim/p2table.py /home/user/perf/sim/p2/dev.
  L5 (render pipeline etc.): render's 88a772a/971eb0c/c5230ff have a truncated GPU2D_Banana.cpp and don't link; rebuilding
  from 1c9095b into /home/user/perf/sim/vb-render (worktree /home/user/perf/sim/v-render); then dev-mode A/Bs
  (base vs head, 3d_pipeline off/on, an A=A noise run) into /home/user/perf/sim/ver/L5-*.
- Queue: L2/L3 (jit, waiting for its post-ASLR re-run + commit), integration check (all branches), findings/sim.md.
- Builds: /home/user/perf/sim/vb-devO3, vb-devpgo (core f503a7a), vb-render. e67 kit binaries kept in /home/user/perf/sim/bin.

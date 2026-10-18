# render: status (overwritten by render only) — HANDOFF-ready; updated 16:47 UTC (date -u)

## Where things are
- Branch `perf/render` @ HEAD = 1c9095b + 1 (render.2d_fence_wait, see `git log`), worktree /home/user/perf/render/sd, from
  00c34c78e8de. 10 commits, every change behind a toggle (default on), exact: 7ebe4e9 render.2d_batch; ea886e8 render.3d_pipeline; dc3f96a 3d_share/3d_wait2d at 1x;
  911ff8e render.2d_comp_unroll; 0bab5aa render.2d_quarters; 2a23af3 render.2d_emit_zip; 347784d the quarters merge fix;
  f76bfca docs/PERF.md; 1c9095b the Gengis workers' wake condition; HEAD render.2d_fence_wait (the waits for the 2D thread
  block on a condvar instead of sched_yield spins: the KMS presenter at SCHED_FIFO spun; device build b9d building,
  TSan x86 rebuild + runs running, matrix m9 + tests on b9d queued; NOT yet in patches/render/: re-export when b9d's
  checks pass: `git format-patch 00c34c78e8de..perf/render -o patches/render/` after `rm patches/render/*.patch`). melonDS patch 0060 (geom_sort) was written, measured
  at -0.1%, rebased out (DEAD-END 16:18; evidence/render/0060-geom-sort-dropped.patch). 0060-0079 unused.
- Deliverables: patches/render/ (9 patches, exported from 1c9095b, `git am` onto 00c34c78e8de checked identical);
  findings/render.md (what, why, numbers, checks, device A/B lines, risks, next); evidence/render/ (every number's file).
- Builds: /home/user/perf/render/b7d = 1c9095b, device toolchain (build-a64-device.sh), the one to measure; b6d =
  f65f846 (same code except the quarters fix); base = /home/user/perf/build-dev/b. Run with /home/user/perf/tools/qemu-dev.
  b3 = f8be6b5 Ubuntu -O3 (matrix m3 vs build-ref). x86tsan = TSan x86 (2D thread only).
- Measured (qemu-count unless said; device builds): comp_unroll membench -5.7% / ds2dtest -9.2% of the emulator's thread
  (rgds-sim: -5.5% / -6.1% ms/frame; LEDGER L1 VERIFIED by sim); pipeline: emulator's thread 3308.6k -> 2324.1k insns
  (sim: 4.344 -> 2.944 ms, -32%, the Gengis threads take the bins; upper bound, qemu starves the render threads);
  1x share: 3116.1k -> 2297.1k (sim -18%); emit_zip: 2D thread output stage -22% (sim: 2D thread -7.1%); quarters fix
  (b7d): 2D thread composite work 812k -> 585k (-28%; v1 was a loss, +4.9% in the sim). LEDGER L5 PENDING (sim).
- Exactness: matrix m3 (ref vs b3) 41 configs FAILS=0; m2 8 FAILS=0; m6 (build-dev vs b6d, qemu-dev) 23 FAILS=0;
  difftest --every toggles off vs on SAME (L5, ds2dtest, geomtest); bananads-tests pass; jit's suite pictures 14 ROMs ok;
  unit tests comp_bench / quarters_test (20000) / zip_test (6000) 0 mismatches; TSan 6 runs 0 warnings.

## Running in the background (results to fold into findings/render.md when they land)
- Done and recorded: m7 (18 configs FAILS=0, tests pass), the 1x question (evidence/render/qc-1x.txt: 2D thread on at 1x
  = -36% L5 / -67% ds2dtest of the emulator's thread; recommendation in findings).
- Done: TSan of the fence wait, 4 runs 0 warnings (evidence/render/tsan-fence.txt, in findings). Running: the b9d chain ->
  /home/user/perf/render/runs/qc-b9d/* (L5 block vs spin), runs/matrix-m9.txt (build-dev vs b9d), runs/tests-b9d.txt.
  If all pass: re-export patches/render/ (10 patches), add m9 to findings, HANDOFF.

## Next directions (the coordinator's, 16:40), each with a CLAIM on the board before starting
1. The frame path to the panels (src/frontend_kms.cpp) at 2x on the Plus: the blocking fence wait is done (HEAD,
   CLAIM 16:42); the per-frame read-through of the presenter (what reads back dumb buffers, what headless.direct
   doesn't exercise) goes into findings "KMS path".
2. The geometry engine's remaining cost on the emulator's thread (0011): RunFast 399k, SubmitPolygonBody 200k, Write32
   101k, SubmitVertexBody 67k on L5 (evidence/render/ref-L5-report.txt): document per-polygon costs; the strip vertex
   copies are a few % of SubmitVertexBody.
3. Thread balance: render.gengis_threads 2 vs 3 with 3d_pipeline on: reason from the sim's per-thread ms (L5 dev mode,
   2 threads: main 2.94, Gengis 3.5/3.4, 2D 4.6 incl. 1.1 of helper bins) and give the device lines (in findings).
4. 2D at 1x without the 2D thread: what remains on the emulator's thread (qc-1x runs + per-symbol).

## If you take over
Read findings/render.md first, then `grep "· render ·" BOARD.md`. Fold the three background results in (section
"Exactness checks done" and "Measurements"), re-export patches/render/ only if the branch moves, post the HANDOFF.

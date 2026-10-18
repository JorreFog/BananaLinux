# render: status (overwritten by render only) — HANDOFF-ready; updated 17:12 UTC (date -u)

## Where things are
- Branch `perf/render` @ eee302e (HEAD), worktree /home/user/perf/render/sd, from 00c34c78e8de. 11 commits, every change
  behind a toggle, exact: 7ebe4e9 render.2d_batch; ea886e8 render.3d_pipeline; dc3f96a 3d_share/3d_wait2d at 1x;
  911ff8e render.2d_comp_unroll; 0bab5aa render.2d_quarters; 2a23af3 render.2d_emit_zip; 347784d the quarters merge fix;
  f76bfca docs/PERF.md; 1c9095b the Gengis workers' wake condition; 8677fad render.2d_fence_wait; eee302e
  render.2d_prefetch (off). melonDS patch 0060 (geom_sort) written, measured at -0.1%, rebased out (DEAD-END 16:18;
  evidence/render/0060-geom-sort-dropped.patch). 0060-0069 unused.
- Deliverables: patches/render/ (11 patches from eee302e; `git am` onto 00c34c78e8de gives the branch exactly);
  findings/render.md (what, why, numbers, checks, device A/B lines, risks, next); evidence/render/ (every number's file).
  Re-export after every new commit: `rm patches/render/*.patch; git -C /home/user/perf/render/sd format-patch
  00c34c78e8de..perf/render -o /home/user/BananaLinux/perf/bananads/patches/render/`.
- Builds: /home/user/perf/render/b10d = eee302e, device toolchain (build-a64-device.sh), the one to measure; base =
  /home/user/perf/build-dev/b. Run with /home/user/perf/tools/qemu-dev. x86tsan = TSan x86 (2D thread only, no Gengis).
- Measured (qemu-count unless said; device builds): comp_unroll membench -5.7% / ds2dtest -9.2% of the emulator's thread
  (rgds-sim: -5.5% / -6.1% ms/frame; LEDGER L1 VERIFIED by sim); pipeline: emulator's thread 3308.6k -> 2324.1k insns
  (sim: 4.344 -> 2.944 ms, -32%, the Gengis threads take the bins; upper bound, qemu starves the render threads; LEDGER
  L5 VERIFIED for 2x by sim); 1x share: 3116.1k -> 2297.1k (sim -18%); emit_zip: 2D thread output stage -22% (sim: 2D
  thread -7.1%); quarters fix (b7d): 2D thread composite work 812k -> 585k (-28%); fence wait: the emulator's thread's
  yield loops gone (device verdict: kms.stats); prefetch: device verdict. Head vs base on the sim (b10d, L5 dev 2x): the
  2D thread 4.765 -> 3.855 ms/frame (-19%), the emulator's thread 4.355 -> ~3.2.
- Exactness: matrices m3 (41 configs), m2 (8), m6 (23), m7 (18), m9 (15), m10 (10): FAILS=0 every time; difftest --every
  toggles off vs on SAME (L5, ds2dtest, geomtest); bananads-tests pass; jit's suite pictures 14 ROMs ok; unit tests
  comp_bench / quarters_test (20000) / zip_test (6000) 0 mismatches; TSan 10 runs 0 warnings.

## The plan since 16:52 (2D-PLAN.md; the user's call) and where I am
- All agents on the 2D engine. My share (17:0x): the compositor (Composite*, BuildSteps, Effects*/Brightness*, DrawBG_3D,
  CompositeTo/EffectsTo, DrawScanline*, the 2x pass structure, render.2d_prefetch), sprites, windows, mosaic, palettes,
  PrepareScanline*, the copy log, the job ring, the Gengis-help question, and the final integration of perf/jit-2d
  (jit: the BG layers DrawBG_*/DrawBitmap*/DrawTiledScroll/ConvertRun16 and the output stage in GPU_Banana.cpp).
  perf/jit-2d and perf/sim-2d had no commits at 17:00; `git log eee302e..perf/jit-2d` shows what to cherry-pick.
- Done since 16:50: render.2d_prefetch committed (eee302e; exact: matrix m10 FAILS=0; qemu-count can't see it: board
  17:01); patches/render/ re-exported (11 patches, git am onto 00c34c78e8de = the branch). render.2d_tiles (a tile-fused
  merge+output pass) written, compile-checked, then SHELVED: it edits jit's output stage and its gain is small
  (evidence/render/tiles-shelved.diff; board 17:01). Head profiles of b10d on rgds-sim: runs/prof/head-*/report.txt
  (the 2D thread at 2x on L5: 3.855 ms, CompositeHires 1.536 ms = 423k insns, memory-bound on the 3D rows; the thread's
  loop 0.147 ms for 22.5k insns: the job ring's stride, see below).
- IN PROGRESS (CLAIM 17:06): render.2d_job_stride=compact|inline (GPU_Banana.h Job: Line3D/Display out of the hot part,
  the ring 128 KB instead of 2.5 MB; a byte stride, JobAt(i)). Code in the worktree, uncommitted; device build b12d
  building (/home/user/perf/render/b12d); then runs/chain-b12d.sh (matrix m12 base vs b12d, rgds-sim inline vs compact
  on L5/L10 dev -> runs/matrix-m12.txt, runs/sim/dev-*-jobstride-ab.txt) and runs/tsan-b12.sh (x86 TSan, 3 runs ->
  runs/tsan-b12.out). If all pass: commit ("render.2d_job_stride"), evidence files, findings, board RESULT, re-export.
- Attribution build for the BG layers (jit's now): /home/user/perf/render/b11a (eee302e, no LTO,
  -fno-inline-functions-called-once); profiles runs/prof/attr-*/report.txt (board 17:10).
- Builds now: b10d = eee302e (the head, device toolchain), b11a (attribution), b12d (job ring). Base build-dev.

## Next candidates in my share (each with a CLAIM first)
- BuildSteps per line (83k insns/frame on L5's 2D thread, 86k membench 1x, 138k ds2dtest: ~2%): cache the step list
  keyed on (enable, priorities, LayerDrawn, LayerFull, OBJPrioPresent, needSecond).
- Sprites: DrawSpriteEntry 1.86M insns/frame on ds2dtest (rotscale), 44k membench; ResolveSprites.
- PrepareScanline* + Banana2DRegs::From + the job on the emulator's thread at 2x (~118k insns/frame on L5 = 4.7%).
- The copy log's volume per frame (the [2d] log line's MB) on ds2dtest/membench.
- The 1x default of render.2d_thread (findings: -36%/-67% of the emulator's thread; a device A/B first).

## If you take over
Read findings/render.md first, then `grep "· render ·" BOARD.md`. Fold the three background results in (section
"Exactness checks done" and "Measurements"), re-export patches/render/ only if the branch moves, post the HANDOFF.

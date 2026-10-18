# render: status (overwritten by render only)

Updated 16:45 UTC. Worktree /home/user/perf/render/sd, branch perf/render @ 1c9095b (9 commits; final unless a check fails). patches/render/ exported from it and git-am-checked.

## Doing now (all chained in the background)
- Device builds: b5d = 0fd83cb (measured), b6d = f65f846 (built; sim verifies the non-quarters toggles on it; m6 matrix on
  it), b7d = 1c9095b (building: the final one). A slip caught by the link: my second quarters edit had cut ~500 lines
  (EffectsPass*, DrawScanline*, PrepareScanline) out of GPU2D_Banana.cpp; restored from 2a23af3's file and folded into
  347784d (every commit's file verified whole).
- Measured so far (device build, qemu-count; board RESULTs 16:12 and 16:16; evidence/render/qc-b5d-*.txt):
  comp_unroll membench -5.7% / ds2dtest -9.2% (emulator's thread, 1x); pipeline: emulator's thread 3308.6k -> 2324.1k
  on L5 device mode (the bins it drew); emit_zip: 2D thread output stage -22%; geom_sort L10 -0.1%; quarters v1 a LOSS
  (fixed @ 971eb0c, to be re-measured on b7d).
- Running/queued: measure-all's tail (dev-L10, 1x share) -> runs/qc-b5d/summary.txt; rgds-sim ab.sh per toggle on b6d
  -> runs/sim/*-ab.txt; matrix m6 (build-dev vs b6d under qemu-dev, 23 configs) + difftest --every + bananads-tests
  -> runs/matrix-m6.txt, difftest-*.txt, tests-b6d.txt; then quarters A/B on b7d (L5, ds2dtest) -> runs/qc-b7d, and
  matrix m7 (build-dev vs b7d, dev2x + 1x-t2-2d) -> runs/matrix-m7.txt.
- TSan: 6 runs, 0 warnings (evidence/render/tsan-runs.txt). sim verifies from b6d/b7d (messages sent 16:08, 16:17).
- rgds-sim (mine, device build b6d; evidence/render/sim/*-ab.txt): comp_unroll membench -5.5% ms/frame, ds2dtest (base vs b6d)
  -6.1%; pipeline: emulator's thread 4.344 -> 2.944 ms (-32%, the Gengis threads take the bins); emit_zip: 2D thread
  -7.1%; quarters v1 +4.9% (the loss); the fix 347784d on b7d (qemu-count): the 2D thread's composite work 812k -> 585k
  (-28%); 1x share: emulator's thread 3.382 -> 2.759 ms (-18%). LEDGER: L1 (comp_unroll) VERIFIED by sim, L5 PENDING.
- Running (the last chain): matrix m7 build-dev vs b7d (dev2x, 2x-t1, 1x-t2-2d), bananads-tests on b7d, then the 1x
  question (2D thread off vs on at 1x) -> runs/matrix-m7.txt, runs/tests-b7d.txt, runs/qc-1x/*. Then HANDOFF.
- Then: findings numbers (quarters on b7d, sim), commit PERF.md, patches/render/, HANDOFF.

## Found (READ)
- Direction 3 (Gengis A55 kernels): nothing to port. bananads/gengis/* is byte-identical to SuperDrastic
  `claude/gengis-a55-scheduling` @ a63fe5b, which descends from `origin/main` @ 930c4c5 (merge-base = main's head;
  `git log a63fe5b..origin/main -- src/rast` is empty).
- Device settings are SETTINGS.md's defaults (run-bananads sets only frontend=kms, render.renderer=gengis, the dirs):
  render.gengis_threads=3 (hgquick.sh uses 2), 2D thread only at 2x, core.slice=64, render.2d_lazy on.
- The 2D job ring: 512 jobs x ~4.9 KB (Job::Line3D[4][256] + Display[256]) = 2.5 MB, far over the 512 KB L3; at 2x
  with render.3d_share the 4 KB Line3D is unused except for scratch lines (X scroll / aborted frame).
- At 1x: render.3d_share / 3d_wait2d apply only at 2x (Quarter set): every line with BG0 3D copies 1 KB into the
  job and the emulator's thread waits for the Gengis bin (GetLine -> WaitBin).

## Plan (in order; CLAIM before each)
1. [done] Baseline numbers + futex wakes per frame (qemu -strace; evidence/render/futex-L5.txt).
2. [done, 7ebe4e9] `render.2d_batch`. No spin (coordinator: a spin steals a core; device-only verdict; dropped).
3. [in progress] `render.3d_pipeline` (direction 1: the VBlank wait for the 3D bins).
4. Direction 1 at 1x: render.3d_share / 3d_wait2d at 1x (no 1 KB copy per 3D line, the bin wait on the 2D thread).
5. Direction 2: the Job ring's stride (4.9 KB jobs; only the dTLB, small) if time permits.
6. Direction 4: the geometry engine's share on the main thread (830k/frame on L5; sim-measurable).

## Next steps for a successor
Read this file, findings/render.md (the deliverable: numbers, checks, device A/B lines) and `grep "· render ·" BOARD.md`.
The branch is complete and exported (patches/render/, 9 patches, git-am-checked). What is still open:
1. Collect the last background results if they are not yet in findings/render.md: /home/user/perf/render/runs/matrix-m6.txt
   (build-dev vs b6d under qemu-dev), runs/difftest-*.txt, runs/tests-b6d.txt (the m6 chain); runs/qc-b7d/* (the quarters
   fix's A/B: expect the 2D thread's composite work below 812k on L5), runs/matrix-m7.txt, runs/tests-b7d.txt, runs/qc-1x/*
   (1x: 2D thread off vs on + share) (the b7d chain). Copy them into evidence/render/ and add the numbers to the findings.
2. If any matrix shows DIFF: find the ROM/config, run abcheck.sh by hand on it (evidence/render/abcheck.sh), and if it is the
   quarters fix (347784d) bisect with render.2d_quarters=off; everything before it is proven exact (m3, m2, suite).
3. HANDOFF post on the board (not done if this file still says so) and the findings' Status line.

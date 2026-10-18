# render: status (overwritten by render only)

Updated 16:35 UTC. Worktree /home/user/perf/render/sd, branch perf/render @ a1da057 (9 commits: a1da057 the Gengis workers wake on a claimable bin alone (a benign pre-existing race dropped), 7ebe4e9 render.2d_batch, ea886e8 render.3d_pipeline, dc3f96a 3d_share/3d_wait2d at 1x, 911ff8e render.2d_comp_unroll, 0bab5aa render.2d_quarters, 2a23af3 render.2d_emit_zip, 88a772a the quarters merge fix, c5230ff PERF.md). Patch 0060 (geom_sort) was rebased out (DEAD-END 16:25); patches/render/ exported from this head (re-export at the end if it moves).

## Doing now (all chained in the background)
- Device builds: b5d = 0fd83cb (measured), b6d = f65f846 (built; sim verifies the non-quarters toggles on it), b7d = the tree
  as of the rebase (88a772a expected; a chained check verifies no GEOMSORT in it, else builds b8d).
- Measured so far (device build, qemu-count; board RESULTs 16:12 and 16:16; evidence/render/qc-b5d-*.txt):
  comp_unroll membench -5.7% / ds2dtest -9.2% (emulator's thread, 1x); pipeline: emulator's thread 3308.6k -> 2324.1k
  on L5 device mode (the bins it drew); emit_zip: 2D thread output stage -22%; geom_sort L10 -0.1%; quarters v1 a LOSS
  (fixed @ 971eb0c, to be re-measured on b7d).
- Running/queued: measure-all's tail (dev-L10, 1x share) -> runs/qc-b5d/summary.txt; rgds-sim ab.sh per toggle on b6d
  -> runs/sim/*-ab.txt; matrix m6 (build-dev vs b6d under qemu-dev, 23 configs) + difftest --every + bananads-tests
  -> runs/matrix-m6.txt, difftest-*.txt, tests-b6d.txt; then quarters A/B on b7d (L5, ds2dtest) -> runs/qc-b7d, and
  matrix m7 (build-dev vs b7d, dev2x + 1x-t2-2d) -> runs/matrix-m7.txt.
- TSan: 6 runs, 0 warnings (evidence/render/tsan-runs.txt). sim verifies from b6d/b7d (messages sent 16:08, 16:17).
- rgds-sim (mine, device build b6d): comp_unroll membench 4.871 -> 4.604 ms/frame @1104 (-5.5%); the rest running
  (runs/sim/*-ab.txt; copies in evidence/render/sim/).
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
Read this file, `tail -n 80 BOARD.md`, then continue the plan from the first unchecked item.

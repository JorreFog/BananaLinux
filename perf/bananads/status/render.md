# render: status (overwritten by render only)

Updated 16:10 UTC. Worktree /home/user/perf/render/sd, branch perf/render @ f65f846 (7 commits: 7ebe4e9 render.2d_batch, ea886e8 render.3d_pipeline, dc3f96a 3d_share/3d_wait2d at 1x, f8be6b5 patch 0060 render.geom_sort, 968fe03 render.2d_comp_unroll, fd53c40 render.2d_quarters, f65f846 render.2d_emit_zip). Patch numbers 0060-0079.

## Doing now (all in the background, chained; nothing else needs CPU)
- Builds: device builds b5d = 0fd83cb (done), b6d = f65f846 (linking, the final one); x86tsan done.
- Running: evidence/render/measure-all.sh build-dev vs b5d -> /home/user/perf/render/runs/qc-b5d/summary.txt;
  TSan runs (2D thread, batches 1..200, lazy on/off, reloads) -> /home/user/perf/render/runs/tsan/*/log.
- Queued: rgds-sim ab.sh per toggle on b6d -> /home/user/perf/render/runs/sim/*-ab.txt; then the matrix on the device
  build (build-dev vs b6d under qemu-dev, 23 configs) -> runs/matrix-m6.txt, difftest --every toggles off vs on (L5,
  ds2dtest, geomtest) -> runs/difftest-*.txt, bananads-tests -> runs/tests-b6d.txt.
- sim has the verification commands (SendMessage 16:08). Then: findings numbers, PERF.md section, patches/render/.
- Exactness so far: matrix m3 ref vs b3 41 configs FAILS=0; m2 8 FAILS=0; jit's suite pictures 14 ROMs ok (its
  difftest tail didn't run: bash parse error in the script); unit tests comp_bench / quarters_test (12000) / zip_test
  (6000): 0 mismatches.

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

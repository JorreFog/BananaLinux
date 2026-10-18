# render: status (overwritten by render only)

Updated 15:40 UTC. Worktree /home/user/perf/render/sd, branch perf/render @ dc3f96a (commits: 7ebe4e9 render.2d_batch, ea886e8 render.3d_pipeline, dc3f96a 3d_share/3d_wait2d at 1x). Patch numbers 0060-0079.

## Doing now
- Builds: /home/user/perf/render/b1 = 7ebe4e9+ea886e8 (2d_batch + pipeline), b2 = dc3f96a (+ 1x share, building),
  x86tsan = ThreadSanitizer x86 build of dc3f96a (building; no Gengis on x86: covers the 2D thread only).
- Running: evidence/render/matrix.sh ref vs b1 (pipeline on) -> /home/user/perf/render/runs/matrix-m1.txt (10 ROMs x
  10 configurations incl. savestate reload). Smoke test L5 device mode: SAME, and the emulator's thread drew 0 of
  1188 bins (ref: 227 of 1188) -> /home/user/perf/render/runs/ab/smoke-b1-L5/b/log.
- Next: matrix for b2 (1x share) and for the toggles off (b2 off vs ref), jit's exact-suite.sh (difftest, tests) on
  b2, TSan runs; then a melonDS patch 0060: a stable radix sort for GPU3D::VBlank's polygon Y-sort (std::stable_sort
  of up to 2048 pointers + a malloc each frame) if the sim/qemu-count shows it matters on polygon-heavy ROMs.
- Baseline done: evidence/render/measure-dev.sh on dsstress L5, ref build, device mode: main thread 3424k insn/frame,
  of which ~1150k are Gengis bins it drew itself (render.gengis_help=1: 227 of 1188 bins under qemu); 2D snapshot
  work ~230k; geometry ~830k (evidence/render/ref-L5-report.txt).
- Build of the unmodified tree /home/user/perf/render/b0 is byte-identical to the reference binary.

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

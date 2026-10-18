# render: status (overwritten by render only)

Updated 15:35 UTC. Worktree /home/user/perf/render/sd, branch perf/render @ 00c34c7 (no commits yet). Patch numbers 0060-0079.

## Doing now
- Baseline: the main thread's instruction count per frame in "device mode" (gengis 2x, 3 Gengis threads, 2D thread,
  headless.direct) under qemu-count per thread: `evidence/render/measure-dev.sh` (runs in /home/user/perf/render/runs).
- Build of the unmodified tree: /home/user/perf/render/b0 (checks the toolchain; ccache warm).

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
1. Baseline numbers + futex wakes per frame (qemu -strace).
2. Direction 5: `render.2d_batch` (KickBatch 16 untuned), a bounded spin before the 2D thread sleeps; count wakes.
3. Direction 2: the small Job (Line3D/Display out of the ring; toggle).
4. Direction 1: 3d_share/3d_wait2d at 1x.
5. Direction 4: the geometry engine's share on the main thread, if large on dsstress.

## Next steps for a successor
Read this file, `tail -n 80 BOARD.md`, then continue the plan from the first unchecked item.

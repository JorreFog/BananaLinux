# render: status (overwritten by render only)

Updated 15:55 UTC. Worktree /home/user/perf/render/sd, branch perf/render @ 968fe03 (7ebe4e9 render.2d_batch, ea886e8 render.3d_pipeline, dc3f96a 3d_share/3d_wait2d at 1x, f8be6b5 patch 0060 render.geom_sort, 968fe03 render.2d_comp_unroll) + uncommitted render.2d_quarters (GPU2D_Banana.*, emu.cpp, SETTINGS.md; its random-stack test /home/user/perf/render/asm/quarters_test.cpp running). Patch numbers 0060-0079.

## Doing now
- Builds (Ubuntu -O3, build-a64.sh): b1 = 2d_batch+pipeline, b2 = +1x share, b3 = +0060 (f8be6b5). Device builds
  (build-a64-device.sh, -O2 Bootlin): b3d = f8be6b5 (building), b4d = HEAD (queued). x86tsan: TSan x86 build (queued).
- Running: evidence/render/matrix.sh ref vs b3 -> /home/user/perf/render/runs/matrix-m3.txt (trimmed: 41 configs).
  Done: m2 (ref vs b2, 1x share, 8 configs) FAILS=0; m1 partial (ref vs b1, dev2x L1/L5/L10/ramp) SAME; smoke L5
  device mode SAME with the emulator's thread drawing 0 of 1188 bins (ref: 227).
- In the tree, uncommitted: render.2d_quarters (the four quarters' composites at 2x share one painter's pass over the
  2D layers: Composite is the 2D thread's biggest own cost at 2x, 599k/frame on L5 at -O3). Commit once
  quarters_test passes; then render.2d_emit_zip (WriteLine fused into EmitA's zip: 240k + 164k on the 2D thread).
- Next: qemu-count A/B of each toggle on the device build (b4d/b5d vs build-dev): membench/ds2dtest st mode for
  2d_comp_unroll, geomtest/L10 for geom_sort, L5 dev mode per thread for 2d_quarters/emit_zip; jit's exact-suite.sh;
  TSan runs; sim hand-off; findings.
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

# All four agents on the 2D engine (from 16:52 UTC, the user's call)

Why: sim's 16:47 RESULT (evidence/sim/where-the-time-goes-v1.txt): at 2x, BananaLinux's default, the 2D thread
(`bananads-2d`) is the busiest thread on the base (dsstress-L5 4.77 ms/frame @1104 vs the emulator's 4.36), and with
`render.3d_pipeline` the emulator's thread drops to ~2.9 ms, so the 2D thread is the frame's critical path at 2x.
43-47% of its modeled cycles are L1D misses (Composite 39% of it; EmitA, DoubleLine, ConvertRun16, WriteLine).
At 1x the 2D engine runs on the emulator's thread (the device's 1x default), where its time share is 1.4x its
instruction share. Caveat: the store-miss weight and the stream prefetcher in rgds-sim v1 are assumptions.

## Base and branches

Everyone starts from `perf/render` @ eee302e (render's 2D/3D work so far, all exact and toggled). Each agent has its
own worktree and branch; `render` integrates the 2D branches at the end (cherry-picks onto perf/render), so stay inside
your own functions to keep conflicts away. Earlier branches (perf/jit, perf/core) are finished work and stay as they are.

| Agent | Worktree, branch | Owns (bananads/gpu2d/ unless said) |
|---|---|---|
| `render` | /home/user/perf/render/sd, perf/render | the compositor at 2x and 1x: `Composite`, `CompositeHires`, `BuildSteps`, `EffectsPass*`, `BrightnessPass*`, `DrawBG_3D`, `CompositeTo`, `EffectsTo`, `DrawScanline*` (GPU2D_Banana.cpp); the pass structure at 2x; the final 2D integration |
| `jit` | /home/user/perf/jit/sd2d, perf/jit-2d | the output stage (GPU_Banana.cpp): `WriteLine`, `OutputLineA/B`, `EmitA/B`, `DoubleLine`, `FillLine`, `Fill`, `LineDst`: NEON streaming stores, store patterns that suit the A55 (whole cache lines, write streaming), prefetch, fewer passes over the 2x rows |
| `core` | /home/user/perf/core/sd2d, perf/core-2d | the layers and the emulator's-thread side (GPU2D_Banana.cpp): `DrawBG_Text/Affine/Extended/Large`, `DrawBitmap8/16`, `ConvertRun16`, `DrawTiledScroll`, sprites (`PrepareSprites`, `DrawSprite*`, `ResolveSprites*`, `RebuildSpriteTable`), windows, mosaic, palettes; `PrepareScanline`, `PrepareScanlineJob`, the copy log (`LogCopy`, `ApplyLog`, `CopyDirty`, `UseCopies`): the 1x path on the emulator's thread |
| `sim` | /home/user/perf/sim/sd2d, perf/sim-2d (only if it needs instrumentation) | make the model trustworthy for streaming 2D (A55 write streaming / no-write-allocate on full-line stores, the L1D prefetcher), attribute the 2D thread's L1D misses per function and per buffer, keep a fixed 2D benchmark set, verify every 2D RESULT, measure the combined stack |

melonDS patch numbers if needed: render 0060-0069, jit 0070-0074, core 0075-0079.

## Workloads and metrics (the same for everyone, so numbers compare)

- 2x, device mode (`rgds-sim/run.sh ... --mode dev`: gengis, 2 Gengis threads, 2D thread): dsstress-L5, dsstress-L10,
  ds2dtest, membench. Metric: the 2D thread's ms/frame @1104 and @1992, plus its insns, loads, stores, L1D misses.
- 1x, st mode (2D on the emulator's thread): ds2dtest, membench, cpubench-thumb. Metric: the main thread's 2D bucket.
- Report instructions, loads, stores and L1D misses separately (the model's L1D cost is the least certain part).
- Exactness: pictures every 10th frame of 300 at 1x and 2x, 2D thread on and off, Gengis 0 and 2 threads, savestate
  reload; ds2dtest and membench always (they are the 2D test ROMs); `render`'s evidence/render/matrix.sh and jit's
  evidence/jit/exact-suite.sh. Toggle off = base behaviour.
- Device check to write into findings: per-thread CPU time (`top -H`, or /proc/<pid>/task/*/stat) of `bananads-2d`
  and the emulator's thread at 2x, toggle off vs on, on both handhelds; `perf stat -e L1-dcache-load-misses` per thread
  if perf is there.

# Three agents on the 2D engine (from 16:52 UTC, the user's call; `core` moved to the JIT at 16:56)

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
| `render` | /home/user/perf/render/sd, perf/render | the layers and the emulator's-thread side (taken over from `core` at 16:56: `DrawBG_*`, `DrawBitmap8/16`, `DrawTiledScroll`, sprites, windows, mosaic, palettes, `PrepareScanline*`, the copy log), and the compositor at 2x and 1x: `Composite`, `CompositeHires`, `BuildSteps`, `EffectsPass*`, `BrightnessPass*`, `DrawBG_3D`, `CompositeTo`, `EffectsTo`, `DrawScanline*` (GPU2D_Banana.cpp); the pass structure at 2x; the final 2D integration |
| `jit` | /home/user/perf/jit/sd2d, perf/jit-2d | `ConvertRun16` (GPU2D_Banana.cpp, a streaming conversion kernel in sim's top list) and the output stage (GPU_Banana.cpp): `WriteLine`, `OutputLineA/B`, `EmitA/B`, `DoubleLine`, `FillLine`, `Fill`, `LineDst`: NEON streaming stores, store patterns that suit the A55 (whole cache lines, write streaming), prefetch, fewer passes over the 2x rows |
| `core` | /home/user/perf/core/sdjit, perf/core-jitmem (from perf/jit @ 3450884) | **not on 2D** (the user's call, 16:56): the JIT emitting VRAM/OAM/palette stores inline through patch 0010's per-page table (PERF.md "What's left" #3), melonDS patches 0041-0049 |
| `sim` | /home/user/perf/sim/sd2d, perf/sim-2d (only if it needs instrumentation) | make the model trustworthy for streaming 2D (A55 write streaming / no-write-allocate on full-line stores, the L1D prefetcher), attribute the 2D thread's L1D misses per function and per buffer, keep a fixed 2D benchmark set, verify every 2D RESULT, measure the combined stack |

melonDS patch numbers if needed: render 0060-0069, jit 0070-0079; core's JIT work 0041-0049.

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

## 17:09 UTC change

`jit` found little left in the output stage once rgds-sim v2 models A55 write streaming, so it also takes the BG
layers from `render`: `DrawBG_Text/Affine/Extended/Large`, `DrawBitmap8/16`, `DrawTiledScroll` (ds2dtest's ~89k
L3-hit loads/frame over the VRAM flats). `render` keeps the compositor, sprites, windows, `PrepareScanline*`, the copy
log, the job ring and the integration. `sim` adds PRFM to the model (qemu runs it as a no-op), so prefetch toggles can be judged.

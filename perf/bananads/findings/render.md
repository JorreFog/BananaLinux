# render: findings (Gengis 3D, the Banana 2D engine, the geometry engine, the threads, the KMS frame path)

Branch `perf/render` (worktree /home/user/perf/render/sd), from 00c34c78e8de. Patches: `patches/render/` (git
format-patch series, 9 patches; checked: `git am` onto a fresh worktree of 00c34c78e8de applies cleanly and the
result is identical to the branch's tree). melonDS patch numbers used: none in the final set (0060 was written and
dropped; 0060-0079 stay free).

Status line (kept current at the end of the session): see "Status" at the bottom.

## What changed (one commit each, every one behind a toggle; defaults as stated)

| Commit | Change | Toggle | Default | Exact? |
|---|---|---|---|---|
| 7ebe4e9 | `KickBatch` (jobs handed to the 2D thread before it is woken) is a setting | `render.2d_batch=N` (1..512) | 16 (as before) | yes: only when the thread is woken |
| ea886e8 | Gengis frames overlap: two frame slots; VBlank waits for the frame's conversion only, not its bins; the emulator's thread stops drawing bins at VBlank | `render.3d_pipeline` | on | yes by construction (checks below) |
| dc3f96a | `render.3d_share` / `render.3d_wait2d` at 1x too: no 1 KB copy of the 3D line into the job, the bin wait on the 2D thread | the existing `render.3d_share`, `render.3d_wait2d` (now 1x and 2x) | on | yes (checks below) |
| 911ff8e | the 2D compositor's step loop unrolled by a fold: at the device's -O2 the plain loop costs 2.5-3x the instructions (core's membench 2D +37% is this) | `render.2d_comp_unroll` | on | yes: the same operations in the same order (comp_bench: identical output) |
| 0bab5aa | at 2x the four quarters of a 3D line share one painter's pass over the 2D layers (the 2D thread's biggest own cost) | `render.2d_quarters` | on | yes: random-stack test, 12000 quarter cases, 0 mismatches |
| 2a23af3 | at 2x the output stage (brightness, 6-to-8-bit expansion) writes straight into the rows: the quarters zipped, 2D lines doubled, no 1 KB line in between | `render.2d_emit_zip` | on | yes: random test vs WriteLineNeon + ZipLine/DoubleLine, 6000 cases, 0 mismatches |
| 88a772a | the quarters merge without the empty halves (the first version lost on shallow stacks, see Measurements) | (same, `render.2d_quarters`) | on | yes: 20000 quarter cases, 0 mismatches |

The first three act on the emulator's thread (2d_batch: its futex wakes; 3d_pipeline: its VBlank wait and the bins it
drew there; the 1x share: a 1 KB copy and a bin wait per 3D line at 1x) and the others on the 2D thread at 2x
(comp_unroll also on the emulator's thread at 1x, where the 2D thread is off by default), which shares the three
other A55s with the Gengis threads, the presenter and the sound thread.

Dropped: a bounded spin before the 2D thread sleeps (the coordinator: it steals a core from a runnable thread on a
4-core device, and the sim can't see that; device-only verdict); melonDS patch 0060, VBlank's polygon Y-sort as a
counting sort (exact, written, measured at -0.1% on dsstress L10 and 0 on geomtest: rebased out, the patch kept in
evidence/render/0060-geom-sort-dropped.patch; see Dead ends). Not done: see "What's next".

## Why (READ, with file:line in the base at 00c34c78e8de)

- Device settings are SETTINGS.md's defaults: BananaLinux's `run-bananads` sets only frontend=kms,
  render.renderer=gengis and the directories. So on the device: `render.gengis_threads=3` (hgquick.sh uses 2), the
  2D thread on only at `render.scale=2`, `render.2d_lazy` on, `core.slice=64`.
- `Finish3DRendering` (VBlank) -> `GengisRenderer3D::FinishRendering` -> `WaitAll` (src/gengis_renderer.cpp:357): the
  emulator's thread waited for all 12 bins and drew missing ones itself (`WaitBin`, `render.gengis_help=1`). Under
  qemu on dsstress L5 that was 227 of 1188 bins: 1150k of its 3424k instructions/frame (evidence/render/ref-L5-report.txt).
  On the device the game log's `[gengis] ... the emulator's thread drew N of M bins` line says how much it happens.
- Gengis's `bananads/gengis/*` is byte-identical to SuperDrastic `claude/gengis-a55-scheduling` @ a63fe5b, which
  descends from `origin/main` @ 930c4c5: nothing newer to port (direction 3 of the brief).
- The 2D job ring is 512 x ~4.9 KB (`Job::Line3D[4][256]` + `Display`): 2.5 MB of address space, but only the ~300
  hot bytes of each job are touched at 2x with render.3d_share, so the cache footprint is ~160 KB; the 4 KB stride
  costs L1 dTLB misses only. Left alone (small).

## Measurements (qemu-count, exact instruction counts; rgds-sim numbers when verified)

Every number has a file in evidence/render/. "Device build" = `tools/build-a64-device.sh` (Buildroot's -O2, Bootlin
GCC 14.3) run under `tools/qemu-dev`; "base" = /home/user/perf/build-dev/b (00c34c78e8de), "mine" = the branch.

- **render.2d_comp_unroll** (the emulator's thread at 1x; st mode: one thread, soft 3D, no 2D thread; device build
  b5d @ 0fd83cb; evidence/render/qc-b5d-st-2d.txt): main thread instructions/frame, off -> on: membench 6027.9k ->
  5685.2k (**-5.7%**), ds2dtest 21224.2k -> 19261.9k (**-9.2%**). Base vs mine: membench 6013.4k -> 5685.2k, ds2dtest
  21212.4k -> 19261.9k. `Composite` on membench: 710.9k (base) -> ~250k. The harness behind it
  (evidence/render/comp-bench.txt): per 256-pixel composite at -O2, 2/3/4 steps: 2588/3966/5665 (plain loop) vs
  1558/1833/2154 (fold) vs 847/1432/1833 (the plain loop at -O3).
  **rgds-sim v1** (st mode = Gengis on the emulator's thread, 1x; device build b6d; evidence/render/sim/st-membench-unroll-ab.txt):
  membench, off -> on: main thread **4.871 -> 4.604 ms/frame @1104 MHz (-5.5%)**, 2.939 -> 2.786 @1992 (-5.2%);
  instructions -9.4%, the 2d bucket 2.258 -> 1.992 ms; L1I misses unchanged (the sim's alternative fit says -3.5%).
  **VERIFIED by sim** (LEDGER L1, its own build and runs): membench 4.820 -> 4.575 ms (-5.1%), ds2dtest 25.041 ->
  23.571 ms (-5.9%).
  Base vs b6d (sim/st-membench-base-vs-b6d-ab.txt): 4.780 -> 4.609 ms (-3.6%), with L1I misses 3.9k -> 6.1k a frame
  (the unrolled compositor is more code; small in absolute terms). ds2dtest, base vs b6d
  (sim/st-ds2dtest-base-vs-b6d-ab.txt): **25.171 -> 23.647 ms/frame @1104 (-6.1%)**, 14.198 -> 13.345 @1992;
  instructions -9.2%, the 2d bucket 22.387 -> 20.900 ms, conditional mispredicts 85.5k -> 74.0k, L1I misses 10.4k -> 13.6k.
- **render.geom_sort** (dropped, see Dead ends; the emulator's thread; st mode; evidence/render/qc-b5d-st-sort.txt):
  dsstress L10, off -> on: 18451.5k -> 18433.4k (-18k, -0.1%); geomtest 4475.6k both ways (its frames have fewer
  than 64 polygons: the std::stable_sort path either way). Exact, but below what the device can see. rgds-sim
  (sim/st-L10-sort-ab.txt, st-geomtest-sort-ab.txt): L10 6.797 -> 6.764 ms/frame @1104 (-0.5%), geomtest 0.00%.
- **render.3d_pipeline** (device mode: gengis 2x, 3 Gengis threads, 2D thread; dsstress L5; device build b5d;
  evidence/render/qc-b5d-dev-L5.txt): the emulator's thread, off -> on: 3308.6k -> 2324.1k instructions/frame
  (**-30%**): the bins it drew at VBlank (`[gengis]` log: 193 of 1188 bins in base, 0 with it). Under qemu the render
  threads are starved by the host, so the share is larger than it will be on the device; the device measure is the
  same log line and the fps.
  Same on dsstress L10 (device mode, base vs mine): the emulator's thread 3147.7k -> 2387.4k (-24%)
  (evidence/render/qc-b5d-summary.txt).
  **rgds-sim v1** (dev mode = gengis 2x, 2 Gengis threads, 2D thread; device build b6d; same binary, off -> on;
  evidence/render/sim/dev-L5-pipeline-ab.txt): the emulator's thread **4.344 -> 2.944 ms/frame @1104 (-32%)**, 2.661 ->
  1.734 @1992; its `gengis` bucket 1.523 -> 0.018 ms (the bins it drew), its L1D load misses 8.4k -> 1.5k. The two
  Gengis threads take those bins: 2.823 -> 3.499 and 2.829 -> 3.386 ms (+20-24%); the 2D thread +0.8%. The work moves,
  the emulator's thread is freed; under qemu the render threads are starved, so on the device the moved share is
  smaller (the `[gengis]` log line says how much). Base vs b6d (sim/dev-L5-base-vs-b6d-ab.txt): the emulator's thread
  4.321 -> 2.919 ms (-32.5%), the 2D thread 4.680 -> 4.619 (its own 2d bucket 3.062 -> 2.761 ms, -10%).
- **render.3d_share / 3d_wait2d at 1x** (1x with the 2D thread and 2 Gengis threads, dsstress L5, device build b5d;
  evidence/render/qc-b5d-summary.txt, qc-b5d/dev1x-*): the emulator's thread, `render.3d_share=off` -> on: 3116.1k ->
  2297.1k instructions/frame (**-26%**: the per-line bin waits, during which it drew bins itself, and the 1 KB copies);
  base (build-dev, the same settings) 3277.8k. Caveat as for the pipeline: the waits' share is an upper bound (qemu
  starves the render threads); the copies (192 KB a frame) are a sure gain. The 2D thread is off by default at 1x:
  this is a reason to A/B `render.2d_thread=on` at 1x on the device (lines below).
  **rgds-sim v1** (dev mode at `render.scale=1 render.2d_thread=on`, 2 Gengis threads, L5, b6d, off -> on;
  sim/dev1x-L5-share-ab.txt): the emulator's thread **3.382 -> 2.759 ms/frame @1104 (-18.4%)**, its gengis bucket
  0.567 -> 0.011 ms, L1D load misses 4.1k -> 1.5k; the 2D thread 1.191 -> 1.900 ms (+0.7 ms: it now waits for, and
  helps with, the bins instead); the Gengis threads -3..-5%. The emulator's thread stays the longest (2.76 vs 1.90 vs
  1.2 ms), so the frame gets shorter by what it lost.
- **render.2d_emit_zip** (the 2D thread at 2x; same runs; b5d had the quarters' zip only, not the 2D lines'
  doubling): the 2D thread's output stage (WriteLine/EmitA/EmitB/DoubleLine/ZipLine/WriteZip) 451k -> 352k
  instructions/frame (**-22%**), the thread's own work 1696k -> 1601k.
  **rgds-sim v1** (dev mode, b6d with the zip and the doubling, off -> on; sim/dev-L5-zip-ab.txt): the 2D thread
  **4.896 -> 4.546 ms/frame @1104 (-7.1%)**, its 2d bucket 3.326 -> 2.795 ms (-16%), instructions -9.6%, L1D store
  misses -9.7%, conditional mispredicts -17%; the emulator's thread unchanged (+1.6% = its helper bins' noise).
- **render.2d_quarters** (the 2D thread at 2x): as first committed (fd53c40, in b5d) a loss on L5's shallow stack:
  composite work 812k -> 890k, because the empty "above" half was materialised (a zero line per line, 164k) and the
  backdrop read as a line. rgds-sim on b6d (= v1), off -> on (sim/dev-L5-quarters-ab.txt): the 2D thread 4.378 ->
  4.590 ms/frame (**+4.9%**: the loss, confirmed). Fixed in the follow-up commit 347784d (the merge's empty-half and
  base cases as templates; 20000 random quarter cases, 0 mismatches). **Re-measured on b7d** (the fix; qemu-count,
  device mode, dsstress L5; evidence/render/qc-b7d-quarters.txt): the 2D thread's composite work, off -> on: **812k ->
  585k instructions/frame (-28%)**, the thread's own work 1465k -> 1230k (-16%); ds2dtest unchanged (its lines never
  show the 3D layer on BG0, so the path is not taken: 12532k both ways). The sim's verdict on b7d is sim's to give.

- Baseline, reference build, device mode (gengis 2x, 3 threads, 2D thread, headless.direct), dsstress L5, frames
  100-300: main thread 3423.9k insn/frame; by bucket: other 1231k (of which rast_kern_h*/gx_polygon/f_run/gx2_render_bin
  ~1150k = bins drawn by the emulator's thread), 3d 831k, core 507k, memory 269k, 2d/gpu 230k, jit-code 206k.
  Threads: Gengis 1288k/1286k/1046k, 2D thread 2481k. evidence/render/ref-L5-report.txt, ref-L5-threads.txt.
- futex syscalls per thread (qemu -strace, same settings): evidence/render/futex-L5.txt.

## Exactness checks done (every toggle on vs the base, unless said)

- Pictures (every 10th frame of 300, rtc.fixed) and the audio dump identical to the reference build (build-ref,
  00c34c78e8de), `evidence/render/matrix.sh` -> `abcheck.sh`: **m3** (b3 = 2d_batch + pipeline + 1x share + the
  dropped sort): 41 configurations, FAILS=0: the device configuration (2x, 3 Gengis threads, 2D thread, headless.direct
  as frontend=kms) on dsstress L1/L5/L10/ramp, Thumb L5/L10, dsswap, ds2dtest, membench, geomtest; the same with a
  savestate reload at frame 150 (L5, L10, ds2dtest, membench, geomtest); 2x with 2/1/0 Gengis threads and without the
  2D thread; 1x with 3/2/1/0 threads, with and without the 2D thread (evidence/render/matrix-m3.txt). **m2** (b2, the
  1x share): 8 configurations at 1x with the 2D thread, FAILS=0. m1 (partial) and the smoke runs:
  evidence/render/matrix-m1-m2-smoke.txt. Determinism: the reference run twice is SAME.
- jit's exact-suite.sh on b3, toggles off vs on (with render.2d_batch=96 on the "on" side): pictures every 50 frames
  on 14 ROMs ok (evidence/render/suite-b3.txt; its difftest tail did not run under this bash: see below).
- Unit tests of the new code against the old on random inputs, device compiler -O2, under qemu: comp_bench (the fold
  vs the plain loop: identical output; evidence/render/comp-bench.txt), quarters_test (the split pass vs the full pass:
  20000 quarter cases, 0 mismatches; quarters-test.txt), zip_test (WriteZip/WriteDouble vs WriteLine + Zip/Double:
  6000 cases, 0 mismatches; zip-test.txt).
- ThreadSanitizer (x86-64 build; no Gengis on x86, so the 2D thread's code only: render.2d_batch 1/16/48/96/200,
  render.2d_lazy on/off, headless.direct, savestate reloads, display capture lines in place on ds2dtest): 6 runs of 300
  frames, 0 warnings (evidence/render/tsan-runs.txt). The Gengis pipeline has no TSan (aarch64 only): its ordering is
  argued in the commit message and exercised by the matrix's thread-count variants.
- On the device-like build under qemu-dev: **m6** (build-dev vs b6d, every toggle on): the device configuration on all
  10 ROMs, with a savestate reload on 5, 2x with 1 Gengis thread, 1x with the 2D thread: 23 configurations, FAILS=0
  (evidence/render/matrix-m6.txt). `bananads-difftest --every --frames 300` (main RAM after every frame), every toggle
  off vs on at 2x with the 2D thread: SAME on dsstress L5, ds2dtest, geomtest; `bananads-tests`: all passed
  (evidence/render/difftest-b6d.txt). m7 on the final head (b7d, with the quarters fix): below.

## Device A/B commands (tools/device/hgquick.sh style; both handhelds, 640x480 RG DS and 1024x768 RG DS Plus, use the
## same settings: the plane scales; run each line against a plain run of the same binary)

hgquick.sh sets `render.gengis_threads=2`; the device default is 3: run the pairs with both (`EXTRA` adds
`--set render.gengis_threads=3`) since the pipeline's gain depends on how the three other cores are shared.

```
# the 3D pipeline (the main change): off vs on (on is the default). The number that decides: the emulator's thread's
# ms/frame (hgquick's fps, and headless.window=60 for the slowest window and frame) with it off vs on, at 2x, on BOTH
# handhelds (RG DS 640x480 and RG DS Plus 1024x768: the plane scales, the emulator's work is the same, but the Plus
# panel's refresh and memory traffic differ), with hgquick's 2 Gengis threads and with the device default of 3
EXTRA="--set render.3d_pipeline=off --set headless.window=60" hgquick.sh <bin>
EXTRA="--set headless.window=60" hgquick.sh <bin>
EXTRA="--set render.3d_pipeline=off --set render.gengis_threads=3 --set headless.window=60" hgquick.sh <bin>
EXTRA="--set render.gengis_threads=3 --set headless.window=60" hgquick.sh <bin>
# and the direct measure of what it removes: the game log's "[gengis] the emulator's thread drew N of M bins" line (at
# exit, and every 3600 frames) in both runs. N with it off is the number of 2x bins (each ~0.5-1.5 ms at 1104 MHz)
# the emulator's thread drew at VBlank per 1188 (12 a frame); with it on N is ~0. Under qemu it was 193 of 1188 on
# dsstress L5 (the render threads starved by the host: an upper bound).

# the 2D thread's batch (wakeups a frame: 16 -> ~24, 48 -> ~8, 96 -> ~4, 192 -> ~2)
EXTRA="--set render.2d_batch=48" hgquick.sh <bin>
EXTRA="--set render.2d_batch=96" hgquick.sh <bin>
EXTRA="--set render.2d_batch=192" hgquick.sh <bin>

# the polygon sort
EXTRA="--set render.geom_sort=off" hgquick.sh <bin>

# the 2D engine at the device's -O2 (the compositor loop), the shared quarters and the fused output stage: these act on
# the 2D thread at 2x (less CPU on the three other cores: read top -H's bananads-2d share, and the fps when the 3D is
# heavy); at 1x the compositor loop is on the emulator's thread
EXTRA="--set render.2d_comp_unroll=off" hgquick.sh <bin>
EXTRA="--set render.2d_quarters=off" hgquick.sh <bin>
EXTRA="--set render.2d_emit_zip=off" hgquick.sh <bin>
EXTRA="--set render.2d_comp_unroll=off --set render.2d_quarters=off --set render.2d_emit_zip=off" hgquick.sh <bin>
EXTRA="--set render.scale=1 --set render.2d_comp_unroll=off" hgquick.sh <bin>
# everything off at once (= the base's behaviour) against the defaults
EXTRA="--set render.3d_pipeline=off --set render.3d_share=off --set render.geom_sort=off --set render.2d_comp_unroll=off --set render.2d_quarters=off --set render.2d_emit_zip=off" hgquick.sh <bin>

# at 1x (render.scale=1): the 2D thread with the 3D line shared (the 2D thread is off by default at 1x)
EXTRA="--set render.scale=1" hgquick.sh <bin>
EXTRA="--set render.scale=1 --set render.2d_thread=on --set render.3d_share=off" hgquick.sh <bin>
EXTRA="--set render.scale=1 --set render.2d_thread=on" hgquick.sh <bin>
EXTRA="--set render.scale=1 --set render.2d_thread=on --set render.gengis_threads=3" hgquick.sh <bin>
```
Correctness on the device: `tools/device/quick.sh`, and the 42-frame HeartGold `rtc.fixed` compare with
`render.3d_pipeline=off` against on (and at 1x with the 2D thread on, `render.3d_share=off` against on).

Two log lines to read in every HeartGold run (the game log, at exit): `[gengis] the emulator's thread drew N of M bins
and converted K frames` (N should be ~0 with the pipeline; a large N without it is the cost the pipeline removes) and
`[2d] 2D thread: J jobs, L lines with unit A drawn in place (display capture), ...`: while a game captures the display
(DISPCAPCNT), unit A's lines, at 2x their four composites, are drawn on the emulator's thread; a large L means the
2D thread isn't taking the top screen's work in that game, which is the next lever (capture's inputs to the 2D
thread, PERF.md's "Next" of the 2D thread section).

## Risks

- `render.3d_pipeline`: new threading (two frames in flight). Exact by data-flow ordering (see the commit message);
  checked only under qemu (no aarch64 TSan). The job ring (512 jobs, ~1.3 frames) and `render.2d_lazy`'s fence at
  line 215 bound how far the 3D can lag: when it lags more, the emulator's thread blocks in `NewJob` / `WaitFence`
  instead of at VBlank (never worse than before: the same deadline, met later). If a game shows a wrong 3D frame or
  a hang with it on and not off, turn it off and report.
- `render.2d_batch` larger than 16: the 2D thread runs further behind within a frame; the presenter's wait for the
  frame's fence (render.2d_lazy) can grow by a batch's drawing time. 48 is a safe first step.

## What's next

- **Display capture on the 2D thread** (the largest lever left for games that capture every frame: while DISPCAPCNT
  is on, unit A's lines, at 2x their four composites, are drawn on the emulator's thread; the `[2d]` log line counts
  them). Not exact by simply deferring the capture: DoCapture writes the captured line into a VRAM bank at the
  line's time, and that bank is read by the CPU and DMA through fastmem at any time, by the texture flats at line
  215 and by the 2D flats on the next lines. Doing it on the 2D thread needs every reader of the capture bank to
  wait for the 2D thread (e.g. the bank unmapped from fastmem while a capture is pending, the slow path waiting), or
  a capture-line queue drained before any such read: a day's work with a device to test on, not an evening's.
- The sim's per-thread numbers for the pipeline (main thread cycles should not change; the bins move to the render
  threads) and for the 1x share (fewer main-thread cycles at 1x with the 2D thread).
- The 2D snapshot work on the emulator's thread at 2x (~105k insn/frame on L5: Prepare*, the job): trims of a few %.
- The job ring's stride (dTLB).
- The geometry engine's remaining share (RunFast 399k, SubmitPolygonBody 200k, Write32 101k on L5).

## Dead ends

(none yet)

## Status

In progress.

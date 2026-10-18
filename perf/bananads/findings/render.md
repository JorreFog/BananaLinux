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
| 347784d | the quarters merge without the empty halves (the first version lost on shallow stacks, see Measurements) | (same, `render.2d_quarters`) | on | yes: 20000 quarter cases, 0 mismatches |
| 1c9095b | the Gengis workers wake on the claim word alone (a benign pre-existing race on the frame number dropped) | (none: no behaviour) | - | yes |
| 8677fad | the waits for the 2D thread (a frame's fence: the KMS presenter at SCHED_FIFO before it shows a slot, the emulator's thread at a frame's end and at line 215; drains; a full ring or log) block on a condition variable instead of spinning on sched_yield | `render.2d_fence_wait=block\|spin` | block | yes: only how a thread waits (checks on b9d below) |
| eee302e | at 2x the 2D thread prefetches the next line's four 3D quarters (4 KB of Gengis's rows) while it merges this line's | `render.2d_prefetch` | off (device verdict) | yes: no effect on the pixels (m10) |
| dc02f53 | the 2D job ring without the 3D-line (4 KB) and display (512 B) arrays between the jobs: the ring the 2D thread reads is 128 KB of consecutive ~256 B jobs instead of 2.5 MB with a job per page | `render.2d_job_stride=compact\|inline` | compact | yes: a layout (m12; TSan 3 runs) |
| 4d1ee7c | the windows' per-pixel loops (ApplyWindow) and the 3D layer's conversion (DrawBG_3D) vectorized; at 2x a windowed line keeps the raw 3D path, the window applied inside the quarters' merge (no converted, masked quarter line per quarter) | `render.2d_win_neon` | on | yes: win_test 5000 cases 0 mismatches (m13) |

The 2D plan (16:52, 2D-PLAN.md): the last three are from the all-hands-on-2D phase (my share: the compositor, sprites,
windows, PrepareScanline*, the copy log, the job ring, the Gengis-help question, the integration of perf/jit-2d).

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
- **2D at 1x without the 2D thread** (the device's 1x default; qemu-count at scale 1 with 2 Gengis threads, device
  builds; evidence/render/qc-1x.txt): the emulator's thread on dsstress L5, 2D thread off: base 3642.6k -> mine
  3561.4k instructions/frame (-2.2%: the compositor unroll, its 2d bucket 712.7k -> 528.0k); 2D thread on (mine):
  **2292.6k (-36% against off)**, and with `render.3d_share=off` 3317.6k. ds2dtest (2D-heavy): 2D thread off 21136.0k
  -> on 6990.6k (**-67%**; the 2D thread takes 12428k). What remains on the emulator's thread at 1x with the 2D thread
  off is the 2D engine itself (528k on L5: text BGs, sprites, the composite; the 2d bucket is 15% of the thread), i.e.
  the 2D thread is the lever at 1x, not another engine pass. Recommendation for the device: A/B `render.2d_thread=on`
  at 1x (lines below) and, if it wins as it did at 2x (39.8 -> 49.6 fps, PERF.md), make it the 1x default too (a
  one-line change in src/emu.cpp: `cfg_.flag("render.2d_thread", scale_ == 2)` -> `true`).
- **render.2d_fence_wait** (b9d, device mode, L5; evidence/render/qc-b9d-fence.txt): spin -> block, the emulator's
  thread 2423.9k -> 2292.3k instructions/frame (the yield loops: `WaitForWorker<WaitFence>` 19.8k + libc 171k -> 107k).
  Under qemu the waits' lengths are host-scheduling artefacts (the 2D thread is starved); what the change is for is
  the device: the presenter at SCHED_FIFO no longer spins while the 2D thread is late. Verdict there: `kms.stats`
  repeats/drops and the fps, spin vs block (lines below).
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

- **render.2d_job_stride** (dc02f53; the 2D thread at 2x): a Job was ~4.9 KB (its Line3D[4][256] copy, 4 KB, and
  Display[256], 512 B, inline), the ring of 512 jobs 2.5 MB: every job the 2D thread took was on a page of its own
  and its ~250 hot bytes on cache lines no stream prefetcher follows; with render.3d_share (the 2x default) the 4 KB
  copy is never even written. Now the arrays are pointers into a block beside the ring (compact, default) or behind
  each job (inline = the old layout), with a byte stride (JobAt). **rgds-sim v2** (dev mode, b12d, inline -> compact;
  evidence/render/jobstride-sim-dev.txt, jobstride-ab/): the thread's loop (SetThreaded2D's lambda, the job reads)
  **0.176 -> 0.112 ms/frame on L5** (-36%), 0.172 -> 0.139 on L10; the 2D thread's **dTLB walks 0.9k -> 0.3k/frame**
  (-62%, -70%); the thread as a whole -2.5% on L5 and +5.0% on L10 with +9.4% instructions, i.e. inside dev mode's
  noise (the helper bins). The loop's instructions are identical (21.5k). Exact: m12 FAILS=0 (23 configurations),
  TSan 3 runs 0 data races.
- **render.2d_win_neon** (4d1ee7c; the 2D thread at 2x, the emulator's thread at 1x, on lines with a window):
  ApplyWindow and DrawBG_3D were scalar 256-pixel loops, and at 2x a windowed line could not take the raw 3D path,
  so every quarter went through both (eight scalar passes a line). Head profile of **membench-win** (membench with
  WIN0 over the 3D scene, the HUD case no test ROM covered; evidence/render/membench-win-main9.c, prof-head/head-mwin-*):
  at 2x the 2D thread 6.409 ms/frame with CompositeHires at 2184k instructions (dsstress L5: 423k) and DrawScanlineFrom
  1741k; at 1x DrawScanlineFrom 1739k = 35% of the emulator's thread (membench without the window: 413k). On ds2dtest
  (windows on both units) ApplyWindow alone is 8.4M of the frame's 19.3M instructions (the attribution build,
  prof-head/attr-ds2dtest-st.txt). Numbers of the change: see below when the chain lands (sim st 2x / st 1x A/Bs on
  membench-win and ds2dtest; the matrix m13).
- **render.2d_steps_cache** (0b5346b): BuildSteps ~220 instructions a call, 83k/frame on the 2D thread at 2x (L5), 86k
  on membench 1x, 138k on ds2dtest (2-3% of the thread's instructions) for the same list on nearly every line; kept
  while its key holds. Numbers: see below (m14 build).
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
- ThreadSanitizer of `render.2d_fence_wait` (the same x86 setup): block with a 200-job batch (L5), block with a
  savestate reload and display capture (ds2dtest: 27888 blocking waits), block without lazy frames (membench), spin
  (L5): 4 runs, 0 warnings (evidence/render/tsan-fence.txt).
- On the device-like build under qemu-dev: **m6** (build-dev vs b6d, every toggle on): the device configuration on all
  10 ROMs, with a savestate reload on 5, 2x with 1 Gengis thread, 1x with the 2D thread: 23 configurations, FAILS=0
  (evidence/render/matrix-m6.txt). `bananads-difftest --every --frames 300` (main RAM after every frame), every toggle
  off vs on at 2x with the 2D thread: SAME on dsstress L5, ds2dtest, geomtest; `bananads-tests`: all passed
  (evidence/render/difftest-b6d.txt). **m7** on the final head (b7d = 1c9095b, with the quarters fix): the device
  configuration on all 10 ROMs, 2x with 1 Gengis thread, 1x with the 2D thread: 18 configurations, FAILS=0;
  `bananads-tests` all passed (evidence/render/matrix-m7.txt). **m9** on b9d (the fence wait commit, every toggle on):
  the device configuration on all 10 ROMs and 1x with the 2D thread: 15 configurations, FAILS=0; `bananads-tests`
  all passed (evidence/render/matrix-m9.txt).

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

# the waits for the 2D thread (the KMS presenter's SCHED_FIFO spin): spin (as before) vs block (the default); read
# kms.stats' repeats and drops (BANANADS_KMS_STATS=600 in /data/banana/bananads.env on the device) and the fps
EXTRA="--set render.2d_fence_wait=spin" hgquick.sh <bin>

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

## The KMS frame path at 2x (direction 1; READ, src/frontend_kms.cpp at 00c34c78e8de)

- With the plane scaling (VOP2 on both handhelds: 512x384 -> 640x480 on the RG DS, exactly 2:1 on the Plus's 1024x768)
  and packed rows, `dp.direct` holds and the Banana 2D engine draws each line straight into the dumb buffers
  (`Emu::draw_into`); the per-frame CPU work of the path is then: emulator's thread `dp.take()` (mutex), `draw_into`,
  `publish` (mutex + an eventfd write), `pace.wait()`; presenter thread `wait2d(fence)` (render.2d_lazy), `overlay()`
  (the pop-ups and the volume pill, only while one shows: that is the one read-modify-write of a dumb buffer), one
  `drmModeAtomicCommit` for both panels. With a shader (`kms.shader`) the GPU pass and its `glFinish` on the
  presenter, not the CPU. No per-frame read-back of a dumb buffer otherwise: the output stage only stores (NEON
  `st4`/`st2`, sequential: the A55's write-combining merges them into full lines); the savestate picture and `F+L2`
  screenshots read a slot (rare); `write()` (the CPU copy, 1.5 MB a frame at 2x) runs only where the plane can't
  scale (QEMU) or with melonDS's 2D renderer.
- What headless.direct does not exercise: the presenter's SCHED_FIFO scheduling and its waits (hence the
  render.2d_fence_wait change: a yield spin at SCHED_FIFO keeps the core from the CFS thread it waits for), the
  commit's timing against vblank (`kms.stats` reports repeats and drops), write-combined memory (headless.direct's
  buffers are cached malloc memory: the same store pattern, no latency difference seen here), the sound thread.
  The device checks: `kms.stats=600` lines (repeats/drops) with render.2d_fence_wait=spin vs block, and the fps.

## The geometry engine's remaining cost (direction 2; READ + qemu-count)

On the emulator's thread in the device configuration (dsstress L5, ref build: evidence/render/ref-L5-report.txt; the
device build's numbers are within a few %: qc-b5d/dev-L5-*.txt): `GPU3D::RunFast` 399k instructions/frame (the
command loop with the FIFO reads, cycle accounting, matrix and lighting commands inline), `SubmitPolygonBody` 200k
(culling, the clip pre-test and clipping, the screen position by one reciprocal per vertex, zero-dot, the polygon
record), `Write32` 101k (the GX FIFO port), `SubmitVertexBody` 67k (the NEON transform and the strip bookkeeping),
`ClipAgainstPlane` 31k: ~800k of the thread's ~2300k with the pipeline on, i.e. a third on this 3D-bound ROM (on
HeartGold the 3D geometry was 11.9% of the thread before 0011, so ~7% now). What 0011 left: the strip's vertex copies
(`TempVertexBuffer` swaps, a Vertex of ~100 bytes once or twice per strip vertex: a few percent of SubmitVertexBody,
~5k a frame here), the FIFO ring (tried and dropped before: more instructions). Nothing of size remains in the
geometry engine short of a different data layout; not pursued this session.

## Thread balance with the pipeline (direction 3; from the sim's per-thread ms, L5, dev mode = 2 Gengis threads)

With render.3d_pipeline the emulator's thread stops drawing bins: on L5 (sim, b6d) it goes 4.34 -> 2.94 ms while
the two Gengis threads go 2.82 -> 3.50 and 2.83 -> 3.39 ms and the 2D thread stays ~4.6 ms (1.1 of it helping with
bins). So on a 3D-heavy scene the render threads become the longest per frame: that is where the device default of 3
Gengis threads (hgquick's 2 is the measuring convention) pays, at the price of 5 busy threads on 4 cores (emulator,
2D, 3 Gengis) plus the presenter and the sound thread. The sim cannot see the contention; the device should test:
`render.gengis_threads=2` vs `3`, each with `render.3d_pipeline=off` vs `on`, on HeartGold (emulator-thread-bound:
expect the pipeline to gain and 2 vs 3 to be a wash) and on a 3D-heavy game (Mario Kart DS, Metroid Prime Hunters:
expect 3 threads to gain with the pipeline), reading `top -H` (the `bananads-2d` and Gengis threads' shares) and the
`[gengis]` line. A hint from the sim: the 2D thread's own 2D work at 2x after this session is ~2.8 ms a frame on L5
(composite 1.2 + output stage 0.5 + the rest), i.e. it leaves most of its core to a Gengis thread.

## Risks

- `render.3d_pipeline`: new threading (two frames in flight). Exact by data-flow ordering (see the commit message);
  checked only under qemu (no aarch64 TSan). The job ring (512 jobs, ~1.3 frames) and `render.2d_lazy`'s fence at
  line 215 bound how far the 3D can lag: when it lags more, the emulator's thread blocks in `NewJob` / `WaitFence`
  instead of at VBlank (never worse than before: the same deadline, met later). If a game shows a wrong 3D frame or
  a hang with it on and not off, turn it off and report.
- `render.2d_batch` larger than 16: the 2D thread runs further behind within a frame; the presenter's wait for the
  frame's fence (render.2d_lazy) can grow by a batch's drawing time. 48 is a safe first step.
- `render.2d_fence_wait=block`: a new condition variable in the hand-off (the 2D thread notifies only while a waiter
  has set the flag; a 1 ms timed wait is the safety net, so a lost wakeup costs at most 1 ms, never a hang). If the
  device shows more repeats in `kms.stats` with it, `spin` is the old behaviour.

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

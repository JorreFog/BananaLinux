# render: findings (Gengis 3D, the Banana 2D engine, the geometry engine, the threads, the KMS frame path)

Branch `perf/render` (worktree /home/user/perf/render/sd), from 00c34c78e8de. Patches: `patches/render/` (git
format-patch series, `git am` onto SuperDrastic at 00c34c78e8de). melonDS patch numbers used: 0060.

Status line (kept current at the end of the session): see "Status" at the bottom.

## What changed (one commit each, every one behind a toggle; defaults as stated)

| Commit | Change | Toggle | Default | Exact? |
|---|---|---|---|---|
| 7ebe4e9 | `KickBatch` (jobs handed to the 2D thread before it is woken) is a setting | `render.2d_batch=N` (1..512) | 16 (as before) | yes: only when the thread is woken |
| ea886e8 | Gengis frames overlap: two frame slots; VBlank waits for the frame's conversion only, not its bins; the emulator's thread stops drawing bins at VBlank | `render.3d_pipeline` | on | yes by construction (checks below) |
| dc3f96a | `render.3d_share` / `render.3d_wait2d` at 1x too: no 1 KB copy of the 3D line into the job, the bin wait on the 2D thread | the existing `render.3d_share`, `render.3d_wait2d` (now 1x and 2x) | on | yes (checks below) |
| f8be6b5 | melonDS patch 0060: VBlank's polygon Y-sort as a stable counting sort (no n log n, no malloc a frame) | `render.geom_sort` / `MELONDS_GEOMSORT=0` | on | yes: the same stable order |
| 968fe03 | the 2D compositor's step loop unrolled by a fold: at the device's -O2 the plain loop costs 2.5-3x the instructions (core's membench 2D +37% is this) | `render.2d_comp_unroll` | on | yes: the same operations in the same order (comp_bench: identical output) |
| fd53c40 | at 2x the four quarters of a 3D line share one painter's pass over the 2D layers (the 2D thread's biggest own cost) | `render.2d_quarters` | on | yes: random-stack test, 12000 quarter cases, 0 mismatches |
| f65f846 | at 2x the output stage (brightness, 6-to-8-bit expansion) writes straight into the rows: the quarters zipped, 2D lines doubled, no 1 KB line in between | `render.2d_emit_zip` | on | yes: random test vs WriteLineNeon + ZipLine/DoubleLine, 6000 cases, 0 mismatches |

The first four act on the emulator's thread (2d_batch: its futex wakes; 3d_pipeline: its VBlank wait and the bins it
drew there; the 1x share: a 1 KB copy and a bin wait per 3D line at 1x; geom_sort: the sort) and the last three on
the 2D thread at 2x (comp_unroll also on the emulator's thread at 1x, where the 2D thread is off by default), which
shares the three other A55s with the Gengis threads, the presenter and the sound thread.

Dropped before coding: a bounded spin before the 2D thread sleeps (the coordinator: it steals a core from a
runnable thread on a 4-core device, and the sim can't see that; device-only verdict). Not done: see "What's next".

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

(filled in as they come; every number has a file in evidence/render/)

- Baseline, reference build, device mode (gengis 2x, 3 threads, 2D thread, headless.direct), dsstress L5, frames
  100-300: main thread 3423.9k insn/frame; by bucket: other 1231k (of which rast_kern_h*/gx_polygon/f_run/gx2_render_bin
  ~1150k = bins drawn by the emulator's thread), 3d 831k, core 507k, memory 269k, 2d/gpu 230k, jit-code 206k.
  Threads: Gengis 1288k/1286k/1046k, 2D thread 2481k. evidence/render/ref-L5-report.txt, ref-L5-threads.txt.
- futex syscalls per thread (qemu -strace, same settings): evidence/render/futex-L5.txt.

## Exactness checks done

(filled in: matrix.sh results, difftest, bananads-tests, TSan)

## Device A/B commands (tools/device/hgquick.sh style; both handhelds, 640x480 RG DS and 1024x768 RG DS Plus, use the
## same settings: the plane scales; run each line against a plain run of the same binary)

hgquick.sh sets `render.gengis_threads=2`; the device default is 3: run the pairs with both (`EXTRA` adds
`--set render.gengis_threads=3`) since the pipeline's gain depends on how the three other cores are shared.

```
# the 3D pipeline (the main change): off vs on (on is the default)
EXTRA="--set render.3d_pipeline=off" hgquick.sh <bin>
hgquick.sh <bin>
EXTRA="--set render.3d_pipeline=off --set render.gengis_threads=3" hgquick.sh <bin>
EXTRA="--set render.gengis_threads=3" hgquick.sh <bin>
# read the game log's "[gengis] ... the emulator's thread drew N of M bins" line in both: with the pipeline N should be ~0

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

- The sim's per-thread numbers for the pipeline (main thread cycles should not change; the bins move to the render
  threads) and for the 1x share (fewer main-thread cycles at 1x with the 2D thread).
- The 2D snapshot work on the emulator's thread at 2x (~105k insn/frame on L5: Prepare*, the job): trims of a few %.
- The job ring's stride (dTLB).
- The geometry engine's remaining share (RunFast 399k, SubmitPolygonBody 200k, Write32 101k on L5).

## Dead ends

(none yet)

## Status

In progress.

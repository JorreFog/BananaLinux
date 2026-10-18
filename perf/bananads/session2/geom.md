# geom: the 3D geometry engine on the emulator thread (session 2, 2026-10-18)

Branch `claude/bananads-perf-geom`, head `402c0b8`, from the pinned `00c34c78e8de`. melonDS patches 0050 and 0051.
Both apply on top of memio's 0021-0022; checked with `git apply` in order on a pristine tree. **Untested on device.**

## Where the geometry cost is after 0011 (qemu, device build, per-TB counts inside the GPU3D functions)

- **dsstress L5** (CPU-paced, lit and textured quad strips: NORMAL, TEXCOORD and VTX_16 per vertex): ~2,850 FIFO
  entries and ~710 vertices a frame. `GPU3D::Run` 385k instructions a frame (~135 per entry, lighting included),
  `SubmitPolygonBody` 195k (~550 per quad), `SubmitVertexBody` 69k, clipping 48k, FIFO writes (memio's side) 122k.
- **gxstm** (memio's ROM: NNS-style draws sent by STM/DMA): ~2,700 entries, `Run` 292k (~103 per entry), FIFO
  unpacking (memio's side) 223k.
- There is no hot spot left. Per entry: loop head and pipe read ~20; the pipe refill with the DMA and IRQ checks
  ~25-40 every other entry; a parameter's store and its cycle ~25; a command's cycle accounting ~25. Polygon
  setup is spread over ~60 small blocks; the vertex transform is already NEON.
- **The finding that matters:** the stream is CPU-paced. `Run` is called ~1,000 times a frame (once per ARM9 slice)
  with only 2.7 (L5) or 5.3 (gxstm) entries each. Its entry and finish (timestamps, FinishWork, GXSTAT bits) cost
  ~90 instructions per call: about a fifth of `Run` on L5. Batching inside one `Run` call can't gain much.

## Done (ranked by expected HeartGold gain)

| # | Commit | Toggle (default) | Expected device gain |
|---|---|---|---|
| 1 | bd49cb0, melonDS 0050 | `render.geom_batch` (`on`; `MELONDS_GEOMBATCH=0` turns it off) | **+0.2-0.5% fps at most** |
| 2 | 402c0b8, melonDS 0051 | CMake `-DBANANADS_GEOMSTATS=ON`, then `MELONDS_GEOMSTATS=<frames>` (off; not in the default binary) | none: a measuring build |

1. **0050:** with `render.geom_fast` and the stall queue empty, `RunFast` does the GX FIFO's DMA and IRQ checks
   once after its commands, not after every pipe refill.
   - Why it's exact: no command reads IF or the DMAs. With the stall queue empty, the FIFO's level only falls,
     so the last check decides both.
   - qemu: `Run` -3.7% (L5), -7% (gxstm), -5.6% (geomtest). That is 2-4% of geometry.
   - Reasoning: geometry is ~13% of HeartGold's emulator thread, and `Run` is about half of that. 4-7% of half
     of 13% is ~0.3%. Probably below the device's noise. It is in the batch because it is exact and free.
2. **0051:** counters for the stream. Every <frames> frames, on stderr:
   - Run calls, with work and stalled;
   - entries per working call, and the queue length at its start;
   - commands by kind (matrix, colour, normal, texcoord, vertex, attributes/material, begin/end);
   - normals by number of enabled lights;
   - polygons.

   I couldn't see HeartGold's stream from here. This tells the next session what it is.

## Device commands (most useful first)

- Build the stats binary: `-DBANANADS_GEOMSTATS=ON`. Then run HeartGold's overworld with `MELONDS_GEOMSTATS=300`
  in the environment, with hgquick's settings, and send the stderr lines. This decides the next step (below).
- `CLK=1104000 EXTRA="--set render.geom_batch=off" hgquick.sh <bin>` against `CLK=1104000 hgquick.sh <bin>`. Expect
  a difference within noise.
- `exact.sh geombatch-on <bin>` and `exact.sh geombatch-off <bin> --set render.geom_batch=off`: the md5 lists
  should match.

## Exactness checks run

- **0050, off against on, all identical:**
  - pictures every 10th of 300 frames on gxstm, geomtest, stress L5 and L10, each with soft 1x, Gengis 2x with
    `render.2d_thread=on`, and a savestate reload at frame 150;
  - `bananads-difftest --every --frames 300` (all of main RAM) on gxstm, geomtest, L5 and iorom-L5.
  - Sabotage check: a build without the deferred DMA check is caught by gxstm's difftest (rc=1).
  - Evidence: `/home/user/perf2/geom/ev/c50/`, `/home/user/perf2/geom/ev-c50.log`.
- **0051:**
  - the default build is byte-identical to the checked 0050 binary (`cmp`);
  - the run-time variant, counters on against off, gave identical pictures on gxstm and geomtest.
  - **Not checked:** the build-option variant itself, with the counters on. The same counting code, but not re-run.

## Tried and dropped

**A command's middle parameters** (a matrix's 2nd to 15th) taken in a short loop, with their cycles added at once.
- Exact: AddCycles(a) followed by AddCycles(b) is the same as AddCycles(a+b).
- gxstm `Run`: -0.1%. The parameters arrive a few per `Run` call (0.6 per batch).
- The diff is in `/home/user/perf2/geom/0051-bulk-params-dropped.diff`. For DEAD-ENDS.md.

## Ranked ideas for next time (expected gain, reasoning)

1. **Lazy geometry: run the engine only when something observes it**, not once per ARM9 slice. Observers are
   GXSTAT and RAM_COUNT reads, CLIPMTX/VECMTX reads, test results, a full FIFO (stall), the FIFO IRQ and DMA
   thresholds, VBlank, and the 3D registers.
   - Gain: up to ~1/5 of `Run`'s instructions (the per-call cost), plus fewer cache misses from the slice loop.
     About 1-2% of HeartGold's emulator thread if its stream is CPU-paced as on L5.
   - Risk: exact cycle timing for GXSTAT busy bits, FIFO-full stalls and the FIFO IRQ means catching up at each
     observer and scheduling an event for the IRQ and DMA thresholds. A day's work, with `loop` (the slice loop).
     Check `MELONDS_GEOMSTATS` on HeartGold first: worth it only if entries per working call is small there too.
2. **NEON lighting** (the three colour channels per light in one vector, all u32 lane arithmetic, so exact).
   - Gain: ~25 instructions per light per normal. 2% of geometry at one light; more with 2-4 lights.
   - Worth it only if the stats show HeartGold's normals with lights enabled.
3. **Strips without the TempVertexBuffer swaps** (rotate indices instead of copying 64-byte vertices).
   - Gain: ~20 instructions per vertex, ~2% of geometry. Savestates must keep the same buffer contents.
4. **Not worth it:** further cuts in polygon setup and the command switch, and matrix stack push/pop/restore.
   Already tight; the copies are 64 bytes.

All of these are small next to the emulator thread's other buckets (JIT code 32%, memory slow paths 18%).
Geometry as a whole is ~13%, and after 0011 it has no single hot spot.

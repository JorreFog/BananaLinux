# mk3geom: the geometry engine's own cost on Mario Kart DS's race start (session 3, phase 3, 2026-10-19)

Branch `claude/bananads-perf-mk3geom` on JorreFog/SuperDrastic, from `3a743f3` (`claude/bananads-perf-mk2`).
Head **`3610c71`** (pushed 13:15 UTC). CI (`bananads-device`, run 39258638511, passed): artifact
**`bananads-aarch64-3610c71a363a0fd39c70720cc6dcc83016c6f20d`**. **Untested on device.**
melonDS patches 0080-0082 (they apply on top of 0072; 0080/0081 touch GPU3D.cpp/.h only, 0082 DMA.cpp/.h only).
Nothing in the JIT, the 2D thread or the SPU. Evidence: `mk3geom-evidence/`.

## Where `GPU3D::Run`'s instructions went (the brief's first question)

qemu per-TB counts of a `-g1` build of `3a743f3` (the same code as the device build, checked by symbol sizes and
addresses), mapped to source lines (`mk3geom-evidence/run-lines-3a743f3.txt`). MK 1650-2150: ~27,400 FIFO entries
and ~2,180 calls a frame, `Run` 2.17M instructions a frame:

| What | Per entry | A frame |
|---|---|---|
| **the loop's own mechanics** (render.geom_fifo's queue read with its pipe-or-FIFO select 7.4, the pipe's refill counting 6.7, the loop test 4.2, GXSTAT's clear flag recomputed from each command 5, the clear 1.8) | **~26** | **~0.72M** |
| command dispatch (`CmdNumParams` lookup, compare, switch; a second switch for multi-parameter commands) | ~11 | ~0.30M |
| cycle bookkeeping (`AddCyclesT`, the pipeline delays, `VertexPipelineSubmitCmd`) | ~14 | ~0.38M |
| per call (setup, the rings' state put back, the GX FIFO DMA and IRQ checks): ~145 a call | | ~0.30M |
| the commands' own work (texcoords, matrix loads/mults, normals' call, vertex calls) | | the rest |

So the loop itself was the biggest piece, then the dispatch. The matrix and vector commands are small on MK (~50
matrix commands a frame); the FIFO pops were the loop's mechanics above.

## Commits (all toggles default `on`, all exact on every check below)

| Commit | melonDS | Toggle | What | qemu, emu thread, MK 1650-2150 (a frame) | Expected on device |
|---|---|---|---|---|---|
| 88388f7 | 0080 | `render.geom_queue` | the command loop reads its queue run by run with a pointer; the pipe's refills worked out after the loop from the number of entries read (`PipeRefills`, closed form); the clear flag set by the commands that lower the counts | `Run` 2,204.3k -> 1,921.4k; **thread -295k (-2.3%)** | **-1..-2%** |
| 8ff5a53 | 0081 | `render.geom_switch` | one switch on the command for every entry (multi-parameter commands go to their path from it, the full vertex has its own); 10 instructions of dispatch instead of 15 | `Run` 1,934.4k -> 1,822.3k: **-112k (-0.9%)** | **-0.5..-1%** |
| 3610c71 | 0082 | `render.gx_dmatime` | `Run9GX`'s burst timing 4 units at a time (one 32-bit table read, the 4 summed) while none ends the table and the sum stays below the target | `Run9GX` 451.0k -> 352.9k; **thread -102k (-0.8%)** | **-0.4..-0.8%** |

**Together** (one binary, all three off vs on, `mk3geom-evidence/qcount-all.txt`): `Run` (+ its old loop) 2,194.0k ->
1,825.5k and `Run9GX` -98.1k: **-0.47M instructions a frame, about -3.7% of the emulator thread** (12.56M at
`3a743f3` -> 12.08M without the Gengis bins qemu's thread timing adds).

**Expected device gain, with the reasoning:** the device profile (mkbase) had `GPU3D::Run` at 13.0% of the thread's
cycles for 3.79M instructions (~1.03 cycles an instruction); since then 0050/0061 took it to ~2.2M, so it is ~8% of
today's cycles. 0080+0081 remove 17% of what is left, and what goes is loop control and dispatch: a data-dependent
pipe-or-FIFO select branch and a refill branch every other entry, a load-compare-branch chain before every
dispatch, and a second dispatch for ~30% of the entries. Those are cheap, L1-resident instructions (their IPC is
probably above the thread's 0.49), so I count them at ~0.6-0.8 of their share: **-1.5..-3%** for the two.
0082: `DMA::Run9` was 5.3% on the device before 0060/0066 split it; the per-word timing loop it shortens is a
dependent chain (byte load -> add -> compare -> branch) that now runs once per 4 words: **-0.4..-0.8%**. **About
-2..-3.5% of the emulator thread for the branch on MK's race start**, on top of mk2 (53.7 fps slowest window at the
base -> roughly +1..+2 fps over whatever mk2 gives). No change in frame pacing or audio (exact). The least certain is
0081 (a smaller jump table index path; GCC kept the code compact, `Run` 0x4324 -> 0x3c0c bytes, the old loop out
of line).

## Device commands (most useful first)

Binary: the CI artifact above. Base: mk2's `bananads-aarch64-3a743f32a9bd8726645980a61768ddf9bf2efa4e`.
`OFF="--set render.geom_queue=off --set render.geom_switch=off --set render.gx_dmatime=off"` gives exactly mk2's
paths in this binary (the old loops are out-of-line copies: `RunFastFIFO`, `RunQueueOld`).
1. `mk.sh` headless at 1992 MHz, frames 1650-2150 and 1700-2700: mk2 vs mk3geom, and mk3geom with `EXTRA="$OFF"`.
   Report the slowest 100-frame window. Two runs each (the gain is a few %).
2. The same with `--set prof.hz=1000`: `GPU3D::Run` should drop by ~15-20% of its samples (with `$OFF` the loop shows
   as `GPU3D::RunFastFIFO`, so compare `Run` + `RunFastFIFO` + `RunQueueOld`); `DMA::Run9GX` by ~20%.
   One toggle at a time if there's time: `--set render.geom_switch=off` alone (0081 is the one whose device gain is
   least certain), `--set render.gx_dmatime=off` alone.
3. `kmsmk.sh` with mk3geom: `kms.stats=60` through the countdown, and the clock the governor settles at.
4. **`exact.sh` on Golden Sun: Dark Dawn and HeartGold**: `exact.sh m3-on <bin>`, `exact.sh m3-off <bin> $OFF`, and
   mk2's binary: the md5 lists must match (GX DMA timing and the command loop are on every 3D game's path).

## Exactness run (details: `mk3geom-evidence/exactness-0080.txt`, `-0081.txt`, `-0082.txt`)

Each toggle off vs on, on the device build under qemu (b1 = +0080, b2 = +0081, b3 = +0082 = the head's binary,
byte-identical to a build of `3610c71`):
- **Mario Kart** (mk-scene.sh, fresh save): the 8 frames and the audio dump identical to the base hashes
  (mkside-evidence/mk-scene-base-md5.txt), on and off; **main RAM every frame 1600-2100 with both CPUs' registers and
  the geometry timing state** (`bananads-difftest --regs --timing`): SAME; **full savestates at 1850 and 2000**
  identical on, off and to the checkpoint's (`a57737e0...`, `f476c39e...`).
- **Per-call traces** (not committed; patches in the evidence folder): every `GPU3D::Run` call's ARM9 timestamp,
  `Timestamp`, `CycleCount`, GXSTAT, both rings' levels/positions, the pipe's 4 slots, the push/pop and test counts,
  the pipelines' state, polygon/vertex counts, IF (and for 0081 `ExecParams[0..3]`) folded into a hash: the same
  hash over all **1,133,371 calls** off and on for 0080 and for 0081. For 0082, mk2geom's `Run9` trace: the same hash
  over all **676,042 calls**.
- **Closed forms checked exhaustively / at random:** `PipeRefills` (0080) against the old per-read counting for every
  pipe level 0-4, FIFO level 0-256 and read count: 135,439 cases, all equal (`piperefills-test.cpp`). 0082's 4-unit
  step against the unit loop on 4 million random states (tables with zeros and large entries, positions, limits,
  targets, clock shifts): all equal (`dmatime-test.cpp`).
- **Test ROMs**: check.sh (gxstm, geomtest, dsstress L5/L10: pictures every 10th of 300 frames, soft / Gengis 2x +
  2D thread / savestate reload; difftests of gxstm, geomtest, dsstress L5, iorom L5): 16/16 identical for each
  change; geomtest-irq (`--regs --timing`): SAME for each; jit's `exact-suite.sh`: FAILS=0 for each.
- **Sabotage** (one trace build per change, each sabotage behind an env variable):
  - 0080: (1) no refill check after a single read from a pipe of 3: trace (and call count) and savestate at 1850
    differ; (2) a pop matrix not setting the clear flag: trace differs (frames, audio, savestate don't).
  - 0081: (3) the full vertex's path clearing `ExecParams[1]` after use: trace differs (nothing else: the next
    multi-parameter command overwrites it); (4) a box test's entries sent to the unknown-command path: geomtest's
    pictures differ (MK sends no box tests).
  - 0082: (5) 4 units taken when their sum reaches the target exactly: trace, call count, savestate and the random
    test differ; (6) a 0 in the 4th table entry not seen: **only the random test catches it** (361,669 of 4M states);
    MK's scene never has the table's end on a 4-unit read's last byte, so no game check here sees it.

## Looked at and not done (for DEAD-ENDS.md)

- `AddCyclesT`'s bookkeeping (~14 instructions an entry, ~0.38M a frame): its state (`CycleCount`, the pipelines,
  the vertex slots) is read by the out-of-line vertex, polygon and lighting calls, so it can't live in registers
  across the loop without moving all of them; a branch-free form is not fewer instructions. Not done.
- The DMA block writer's push/pop/test class check per entry (6 instructions) as a table lookup: -4 an entry on
  qemu, but a load feeding a branch on the A55 in place of two predicted compares; geomlazy's per-command version
  grew the code (+25k). Not done.
- Polygon setup (1.14M, 3,300 calls): the hottest line is the clip-plane outside test (80k); nothing over 7%.
  Vertex submission (0.78M): the NEON transform plus colour/texcoord stores, ~95 a vertex. Not done.
- `Run`'s per-call cost (~145 instructions a call after 0080, ~0.3M a frame): the pipe's slot copy (~33 a call), the
  four DMA channels' mode checks (~32), the runs' setup and spills. ~50-100k to gain, not done.

## What the next session should know

- **Left in the geometry (qemu, per frame, at 3610c71):** `Run` 1.82M (dispatch ~10 an entry, `AddCyclesT` ~14, per
  call ~145, the commands' work), `WriteGXFIFOBlock` 1.14M (~36 a word: the word's decode, the entry store, the
  class checks), polygon setup 1.14M, vertices 0.78M, lighting 0.64M, `Run9GX` 0.35M (the burst timing's last few
  units of each call and ~100 a call), clipping 0.21M. Nothing over ~0.15M is left as one compact item.
- **A source-line profile under qemu is cheap and exact:** build with
  `-DCMAKE_C_FLAGS/-DCMAKE_CXX_FLAGS="... -O2 -g1 ..."` and `-DCMAKE_EXE_LINKER_FLAGS=-g1` (same code as `-g0`:
  check `nm -S`), run gl/tbc.sh, then `/home/user/perf3/mk3g/tools/lines.py <bin> <tbc dir> 500 <lo> <hi>` (per
  source line, inline chains with an extra argument). `tools/tbtop.py` gives per-TB counts with executions.
- **Traces beat savestates for loop restructurings:** two of the six sabotages here left frames, audio and the
  frame-end savestate identical; the per-call `Run` hash caught them. Hash anything the change touches (0081's first
  trace didn't include `ExecParams` and missed sabotage 3). Patches: `trace-sab*-build.patch`,
  `dmatrace-sab3-build.patch` in the evidence folder.
- A closed form for a counting loop is worth a standalone exhaustive or random test against the old loop: it is
  cheaper than an emulator run and it catches what the game never reaches (sabotage 6).
- **Next candidates:** the JIT's GX port STRs (mk3jit's), the DMA's per-call cost and `MRAMBurstTable`'s copy (the
  slice loop ends the burst ~1,240 times a frame), and only on device evidence `AddCyclesT` (if the profile shows the
  pipelines' branches mispredicting, a branch-free form might pay there even with the same instruction count).

## Follow-up (13:19-13:26 UTC): polygon setup, vertices, lighting: no commit, a dead end by measurement

**Head unchanged: `3610c71`**, CI artifact `bananads-aarch64-3610c71a363a0fd39c70720cc6dcc83016c6f20d`. No 0083/0084.
I stopped early on purpose: none of the three has a compact exact cut worth a toggle (the coordinator asked for a
real one or a write-up). Numbers: `mk3geom-evidence/followup-polygon-vertex-lighting.txt`.

- **Polygon setup** (`SubmitPolygonBody`, 1.14M a frame, 3,312 calls; ~2,560 pass culling and ~1,280 are stored).
  By source region it is flat: screen coordinates 169k, the culling normal and dot product 163k, Z/W 148k, the
  copy into vertex RAM with the final colours 145k, the outside test and clip calls 125k, bounds and W size 108k,
  and the rest under 80k each. Every region is per-vertex arithmetic that GeomFast already trimmed (reciprocals,
  the outside test before clipping, Z reused along strips). The largest thing one change could remove is ~30-50k
  (0.3%). The far plane's quick reject doesn't help: `ClipAgainstPlane<2>`'s 103k is mostly the near-plane pass,
  which is real clipping.
- **Vertices** (`SubmitVertexBody`, 0.78M, ~95 a vertex): the NEON transform, the colour and texcoord stores, the
  strip bookkeeping and `AddCyclesT(3)`. A colour precomputed at `VertexColor`'s writes would save ~5 a vertex
  (~40k). Not worth a toggle.
- **Lighting** (0.64M, ~145 a normal with ~1 light): the enabled-light loop is already tight. **A result cache is a
  dead end, measured.** In an instrumented build, MK never sends the same normal twice in a row (0 of 2.1M). A
  256-entry cache keyed on (normal, lighting state) hits ~20%, so it would save ~110k but cost ~80k in the lookup
  and the state checks.

For DEAD-ENDS.md: "(mk3geom) MK's polygon setup, vertex and lighting paths have no compact exact cut left (each
region ≤170k a frame, spread out). A lighting result cache: consecutive normals never repeat; a 256-entry cache hits
~20%, which roughly pays for its own lookup."

Next, if geometry stays a target: the remaining cost only falls with structural work. Options are a NEON
per-polygon setup (the 4 vertices' screen coordinates and Z together) or fewer `Run` calls (the slice loop). Both
are bigger than an hour and both need device numbers first to show they would pay.

## Audit (13:38-13:45 UTC): the geometry math on a worker thread (`render.geom_thread`): no-go

The coordinator asked whether the emulator thread could reach 8M instructions a frame by moving the geometry math
to a worker. **It can't, and nothing was built.** Head unchanged: `3610c71`.

- **The numbers at 3610c71** (qemu, per frame): `Run` 1.82M, `WriteGXFIFOBlock` 1.14M, `Run9GX` 0.35M and
  `WriteToGXFIFO*` 0.16M are about 3.5M of command timing, FIFO and DMA, and they must stay on the thread. The math
  is about 2.8M: polygon setup 1.14M, vertices 0.78M, lighting 0.64M, clipping 0.21M, `NextVertexSlot` 0.11M.
  Moving every bit of the math at no cost would still leave the thread at about 9.2M.
- **Timing depends on the data.** `SubmitPolygon` sets `PolygonPipeline` to 8 or 26/35 and sets `VertexSlotsFree`
  from what happens to each polygon:
  - culling (the dot product of the transformed positions);
  - the vertex count after clipping (the far-plane reject with attr bit 12, and the strip clipstart quirk);
  - polygon/vertex RAM full;
  - the zero-dot reject (screen X/Y and W, when attr bit 13 is clear).

  The next vertex commands' cycles read this at once, and so does how far each `Run` call gets. From there the FIFO
  level, GXSTAT, the DMA and the IRQ follow. So the transform, culling, clip positions, the viewport transform and
  the strip-reuse decision must stay on the emulator thread, unless it syncs with the worker about 3,300 times a
  frame.
- **What doesn't depend on the data:** lighting's cycles (the number of enabled lights) and the matrix commands'
  cycles and stack bits. GXSTAT's test result bit, the test results and the matrix reads need the matrices, which
  stay. RAM_COUNT and RAM full are counts the thread keeps. The render list and the Y-sort need a drain at the
  flush/VBlank; savestates need a drain before saving and the worker re-seeded after loading.
- **What could move exactly:** lighting, the vertex colour and texcoord stores, the polygon setup after the
  decision (Z/W, bounds, fields, the vertex RAM copy) and the clip attributes. That is about 1.3M off the thread;
  feeding the ring adds about 0.3-0.4M back, so the net is **about -1M (12.0 -> about 11.0M)**, with the worker
  running about 1.6M. On the device that needs a free A55, and Gengis's bin threads and the 2D thread already share
  the four.
- **Verdict:** no-go for the 8M target. A partial offload worth about 8% could be a later project once the device
  shows a core to spare. To get under 9M, the command timing itself (`Run`, the FIFO writer, the DMA: about 3.5M)
  and the JIT side would have to get cheaper.

## The exact offload (14:13 UTC): patch 0083, `render.geom_thread` (default `on`)

**New head: `649e008`**. CI run 39266964359 passed: artifact **`bananads-aarch64-649e0087632904ca057fa42e7801696bd59adffe`**. After the audit, the user chose the exact offload. Built, exact on every
check, so it is on by default. `render.geom_thread=off` gives 3610c71's polygon setup in this binary.

**What came out smaller than the audit:** lighting is **not** on the worker. The emulator thread clips each polygon
(it needs the vertex count for timing), and clipping interpolates the vertex colours that lighting produces. Moving
lighting would mean the emulator thread clips positions only and the worker clips again with the attributes. That
is a second, larger change, not done here. So the worker does only **the build of a stored polygon**: its fields,
bounds, W size, Z/W, and the copy of its new vertices into vertex RAM with their final colours (`GeomBuild`, the
same statements as before).

**Design.**
- The emulator thread clips straight into the next slot of a single-producer, single-consumer ring of 256
  `GeomJob`s (the clipped vertices with their screen positions, the reciprocals, the polygon's and new vertices'
  places, the shared vertices' places, the state the build reads), so a stored polygon needs no copy.
- It counts `NumPolygons`/`NumVertices` exactly as the build would (RAM_COUNT and RAM full stay right), keeps a
  shadow of the last strip polygon's vertices (the next polygon's sharing test, clipping and zero-dot read it), and
  publishes the slot with a seq_cst head store.
- The worker spins 256 times on an empty ring, then sleeps on a condition variable. It is woken every 32 jobs (only
  if asleep), on a full ring (the emulator thread then waits for a slot), and at a drain. There is no
  `pthread_cond_signal` per job.
- A rejected polygon reuses its slot.

**Sync points (drains):**
- `VBlank()`, first thing (before the translucent split, the Y-sort and the RAM swap the renderer reads);
- `DoSavestate` (save and load; after a load the shadow is seeded from the polygon RAM);
- `Reset`, and the destructor (drain, quit, join).

Nothing else on the emulator thread reads the polygon or vertex RAM. The emulator thread never reads worker results
mid-frame: GXSTAT, RAM_COUNT and the test results don't depend on them, and the strip sharing uses the shadow.

**qemu, MK 1650-2150** (`mk3geom-evidence/qcount-0083.txt`):
- The emulator thread: `SubmitPolygonBody` 1,133.3k -> 903.8k (-229k).
- The hand-off costs the emulator thread ~110k of the 424k moved: ~78k for the job header and the strip shadow,
  ~33k for the slot.
- By the totals, the thread is **12,042.9k -> 11,876.8k without the Gengis bins** (the difference from -229k is
  qemu's thread-timing noise).
- **The worker: ~507k a frame**: the build 424k, its loop, spin and atomics 83k.

**Short of the 11.0M target:** that estimate assumed lighting moved too.

**Device:** expected -1..-2% of the emulator thread's cycles **only if a core is free**. Gengis's bin threads and the
2D thread already share the four A55s, so the worker can cost more than it saves, and **the device decides**. A/B
with `--set render.geom_thread=off`, and watch the 2D thread's and Gengis's frame times as well as the emulator
thread's. Untested on device.

**Exactness** (`mk3geom-evidence/exactness-0083.txt`), off vs on:
- MK's 8 frames and audio = the base hashes (also the head's binary with its default on, and off);
- main RAM every frame 1600-2100 with `--regs --timing` SAME;
- full savestates at 1850 and 2000 identical to the checkpoint's;
- the `Run` per-call trace: the same hash over 1,133,371 calls;
- check.sh 16/16, geomtest-irq SAME, exact-suite.sh FAILS=0.

**Sabotage** (`MELONDS_GEOMSAB`, read at creation): (1) no full-ring check and no batch wake, so the ring
overflows: MK's scene fails (crash). (2) No drain: the scene fails (crash). Both are caught, but both are loud.
A subtler "drain missing only at savestates" sabotage was not run.

**For the next step (lighting on the worker):** the emulator thread would clip with `attribs=false` (positions and
counts only). The worker would clip again with attributes on vertices whose colours come from its own lighting.
That needs a per-vertex record in the ring (position, raw texcoords, the light/material/VecMatrix versions) and
the worker owning `VertexColor`. It could move ~0.64M (lighting) + ~0.15M (vertex attribute stores) more, at
~0.2M of ring cost on the emulator thread.

## Step 2 (14:16-15:10 UTC): lighting on the worker (0084, `render.geom_light`): NOT committed, not exact

**Head unchanged: `649e008`** (0083, `render.geom_thread` on), CI artifact
`bananads-aarch64-649e0087632904ca057fa42e7801696bd59adffe`. 0084 is in `mk3geom-evidence/0084-geom-light-WIP.patch`
(it applies on 0083) with `exactness-0084-WIP.txt` and `qcount-0084-WIP.txt`.

**0083's subtle sabotage, run first: no drain at a savestate save only (`MELONDS_GEOMSAB=3`, a test build).** The
savestate compare catches it at 1850 (`7d00a6f6...` vs `a57737e0...`) but not at 2000, where the worker happened to
be done. So whether it is caught depends on the race; the crash sabotages are the reliable ones.

**The 0084 design.**
- The normal command keeps the texcoords it makes, the transformed normal and its cycles (popcount of the enabled
  lights, at least 1: data-independent) on the emulator thread.
- It queues a light entry in a light ring of 4096, with state snapshots in a ring of 64 when 0x30-0x34 changed them.
- A vertex submitted after it carries the entry's number in its colour (bit 31).
- The emulator thread clips as before: positions, counts and texcoords stay right, and the colours are garbage it
  never keeps.
- The worker lights the entries in order. A stored polygon with lit inputs brings them in its job; the worker
  relights them and redoes the same clipping passes with the attributes (`ClipAgainstPlane` now takes the polygon
  attributes) to get the colours.
- At a savestate (drained), `VertexColor` and the temp vertices' colours are made literal again.

**Where it ended:**
- Exact on: the MK scene, main RAM every frame with `--regs --timing` (after a fix: the worker could light ahead of
  a polygon job and then loop forever, which hung a difftest), savestates at 1850/2000, the `Run` trace,
  geomtest-irq, and exact-suite (FAILS=0).
- **Not exact on `gxstm`**: check.sh's gxstm pictures (soft, Gengis 2x, reload) differ off vs on; the other 13 sets
  and difftests are identical. Not debugged (no time). Suspects: a colour source the encoding misses (lit vertices
  resolved from a light result that was overwritten), or the clip replay in `GeomColors` for a polygon kind MK
  doesn't send. Start there with a per-polygon colour hash, on vs off.
- **No gain anyway:** `CalculateLightingBody` -638.6k on the emulator thread, but `GeomLightCmd` +436k (~100 a
  normal: the texcoord product, the normal's transform, the cycles, the entry) and `SubmitPolygonBody` +103k (the
  inputs' and temp colours' copies). **The emulator thread: 11,876.8k -> 11,868.4k (~0); the worker 507k -> 1,421k.**
  A first version, with the light jobs in the polygon ring, cost more than lighting itself (the ring's 1 KB slots
  and the waits).

**What it would need to pay:**
- the emulator thread doing only the timing per normal (`AddCyclesT(popcount)` and storing the raw normal), with
  the transform and the texcoords on the worker (texcoords then resolved like the colours);
- no copy of the inputs (the worker keeping its own temp-vertex mirror from per-vertex entries);
- then fixing the gxstm difference.

That is a design for another session. **The 11.0M target is not reached: the head is at ~11.88M** (qemu, emulator
thread without Gengis bins, MK 1650-2150; 12.56M at mk2's 3a743f3).

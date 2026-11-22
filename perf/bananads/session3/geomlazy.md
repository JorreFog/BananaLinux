# geomlazy: the geometry engine in Mario Kart DS's race start (session 3, 2026-10-18)

Branch `claude/bananads-perf-geomlazy` on JorreFog/SuperDrastic, from `claude/bananads-perf-mkbase` (9aa478c).
Head **`12f4a5a`** (the code is `1cd2e36`'s; `12f4a5a` adds geomtest's FIFO IRQ phase). CI (`bananads-device`): artifact
**`bananads-aarch64-12f4a5a00d78813853a63cc02bc5d4e07d32b43f`** (same binary as `bananads-aarch64-1cd2e3648b3b4c00397b960517935aa04163bd74`)
(earlier heads built too: `...7e14fc4...` = up to 0060). **Untested on device.**

It needs nothing from session 2's memio branch; it includes session 2's geom 0050 (cherry-picked, unchanged).
melonDS patches: 0050 (session 2), 0060, 0061, 0062.

## The answer to "lazy geometry"

Not built, on purpose. Measured on the scene first (`geomlazy-evidence/stream.txt`): the race start sends ~29,000
FIFO entries a frame, ~31,700 words of them by the **GX FIFO DMA** (mode 7), with the FIFO under half all the time
(so the DMA restarts at every burst's end), GXSTAT's FIFO IRQ in mode 2 (IF bit toggling ~270 times a frame),
~2,180 `Run` calls with 13 entries each. In melonDS the timing is defined by *when* `Run` is called, command costs
depend on the commands' results (culling), and here nearly every slice end is an observation point. Batching `Run`
calls could save its per-call cost, ~1.5% of the thread, at the price of a log-and-replay engine. The full list of
observers and the reasoning: `geomlazy-evidence/observers.md`.

What costs the thread is per entry and per word, so that is what the branch cuts, exactly:

## Commits (all toggles default `on`, all exact on every check below)

| Commit | melonDS | Toggle | What | qemu, emu thread, MK 1650-2150 | Expected on device |
|---|---|---|---|---|---|
| 23efc81 | 0050 (session 2's) | `render.geom_batch` | the GX FIFO's DMA and IRQ checks once per `Run`, not every pipe refill | `Run` 3.79M -> 3.09M | **-2.4%** of the emu thread |
| 7e14fc4 | 0060 | `render.gx_dmafast` | the GX FIFO DMA's block unpacked with its state in registers | `WriteToGXFIFOBody` + `DMA::Run9` 3.58M -> 2.01M | **-5%** |
| 0dedd00 | 0061 | `render.geom_fifo` | `Run`'s loop reads the pipe and the FIFO as one queue | geometry 7.38M -> 7.14M | **-0.8%** |
| 1cd2e36 | 0062 | (`render.gx_dmafast`) | the DMA block's burst timing with the target in registers | `DMA::Run9` 1.86M -> 1.77M | **-0.3%** |
| 61386d8, 93ba8a9 | | | `bananads-difftest --input --from --fresh-save --regs --timing` (tools) | | none |

Together (qemu, `geomlazy-evidence/qcount.txt`): the emulator's thread **15.78M -> 13.25M instructions a frame
(-16%)**, geometry + GX DMA 9.64M -> 7.05M. (Without the Gengis bins/2D lines the emu thread draws itself, which
vary with thread timing under qemu. The 0060 commit message's "16.23M -> 13.82M" mixed in that noise; PERF.md has
the right numbers.)

**Expected device gain, with the reasoning:** the device profile gives `Run` 13.0%, `WriteToGXFIFOBody` 6.4%,
`DMA::Run9` 5.3%. Scaling each share by its instruction cut (the removed instructions are loads, stores and
branches on L1-resident state: the FIFO rings, the command state; the display list reads from main RAM stay):
13.0% x 18% (0050) + 11.7% x 44% (0060) + 13.0% x 6% (0061) + 11.7% x 2.4% (0062) = **~-8.5% of the emulator
thread's cycles in the race start** (range -6..-9%: some of `WriteToGXFIFOBody`'s share may be main-RAM misses that
stay). At 1992 MHz that is ~29.9M -> ~27.4M cycles a frame: the slowest 100-frame window 53.7 -> ~58 fps. Not the
whole -15%: the rest has to come from mkside's areas (frame loop, SPU) and the JIT. No change in frame pacing or
audio behaviour (exact: same frames, same audio bytes).

## Device commands (most useful first)

Binary: the CI artifact above (`bananads`). Base: the mkbase build the brief used.
1. `mk.sh` headless at 1992 MHz, frames 1650-2150 and 1700-2700: base vs geomlazy; report the slowest 100-frame
   window. To separate the code from the layout, also geomlazy with
   `EXTRA="--set render.geom_batch=off --set render.gx_dmafast=off --set render.geom_fifo=off"`
   (exactly the old paths).
2. The same with `--set prof.hz=1000` (the branch has the profiler from mkbase): `GPU3D::Run`,
   `WriteToGXFIFOBody<true>` (should be nearly gone) and `DMA::Run9` are the symbols to compare.
3. `kmsmk.sh` with geomlazy: `kms.stats=60` through the countdown, and the clock the governor settles at after.
4. **`exact.sh` on Golden Sun: Dark Dawn and HeartGold**: `exact.sh gl-on <bin>`,
   `exact.sh gl-off <bin> --set render.geom_batch=off --set render.gx_dmafast=off --set render.geom_fifo=off`, and
   the base binary; the md5 lists must match. Golden Sun is the timing-sensitive one.

## Exactness run (details: `geomlazy-evidence/exactness.txt`)

- Mario Kart (mkside's scene, fresh save): frames 1700/1900/2100/2400 and the audio dump identical to 00c34c78;
  mk-scene.sh's 8 frames + audio = the mkbase reference; savestate reload at 1850 identical; **full savestate bytes at
  frames 1850 and 2000 identical to 00c34c78**; main RAM every frame from 1600 to 2400, toggles off vs on (and with
  CPU registers and the geometry engine's timing state), identical.
- gxstm (session 2 memio's ROM), geomtest, dsstress L5/L10: pictures (soft, Gengis 2x, savestate reload) and
  difftest, each toggle off vs on: identical. jit's `exact-suite.sh`, all toggles off vs on: FAILS=0 (on the
  0061 build and on the final one). Mario Kart with the soft renderer (1x), every 100th frame to 2100: identical.
- Sabotage: 0060 (an entry into an empty FIFO put in the FIFO, not the pipe) caught by MK's difftest (and gxstm
  hangs); 0061 (the deferred DMA/IRQ checks see the FIFO level from before the loop: a DMA one run late) caught by
  MK's difftest; 0062 (a burst's first unit timed as sequential) caught **only by the savestate compare**. Not caught
  and why: a GXSTAT busy bit not set by the block (unobservable: always already set), a missing check when the FIFO
  ran empty (MK's DMA is running then and its IE lacks the GX FIFO IRQ).

## Follow-ups after the coordinator's review (23:43-00:00)

1. **The uncaught sabotage is now caught.** geomtest has a FIFO IRQ phase (commit 12f4a5a): GXSTAT's FIFO IRQ in
   IE (IME on, CPU IRQs masked, IF polled), the FIFO filled with BOX_TESTs (slower than the writers, so it fills and
   the CPU stalls) by the CPU and by a GX FIFO DMA (mode 7) of random length that ends; IF acknowledged while the
   FIFO is full enough, then polled until "empty" / "less than half"; poll count, timer 0 and FIFO level logged to
   RAM. 0061's sabotage "no deferred check when every refill of a run found fewer than 2 entries" (the FIFO running
   empty): **bananads-difftest DIFFERENT after frame 1**. The real build: SAME over 100 frames (geom_fifo off/on, and
   all three toggles off/on); its savestate at frame 60 is identical with all toggles off and on, and to 00c34c78's
   except 4 bytes in the SPI firmware section (they differ between binaries regardless of the toggles; not geometry
   state). The new ROM: `bananads/tests/geomtest/build.py` (copy at `/home/user/perf3/gl/geomtest-irq.nds`; the shared
   `/home/user/perf/roms/geomtest-geomtest.nds` is still the old one). Still not caught by it: 0062's DMA timing
   sabotage (a few cycles per burst stay inside one slice); Mario Kart's savestate compare catches that one.
2. **No further geometry change**: by the qemu counts none is worth a toggle. SubmitPolygonBody (1.14M, ~570 per
   call) has no TB over 11% (culling's normal, then setup spread over ~20 blocks); SubmitVertexBody (0.78M) is 40%
   the NEON transform itself; `Run`'s per-call cost is ~50 instructions in and ~40 out, x 2,180 = ~0.2M (1.5%), and
   only 1.5% of calls find nothing to run, so it can't be skipped without the lazy log/replay. The precomputed
   per-command flags in the DMA writer were tried and grew it (+25k).

## Combined with mkside

A local test merge of this head with `claude/bananads-perf-mkside` e855c82 (0030, 0031, 0070, render.2d_kick):
no conflicts, the 27 melonDS patches apply in order (0001-0020, 0030, 0031, 0050, 0060-0062, 0070), MK's frames,
audio and savestate at 1850 = base; qemu emu thread 15.78M (base) -> 13.25M (geomlazy) -> 13.06M (both). Neither
branch changes what "the geometry engine is idle" means (mkside's halt_skip/slice_chain test it), so they compose.

## Tried and dropped (for DEAD-ENDS.md)

- Lazy geometry (batched `Run` calls): not built; see above and `observers.md`. Worth ~1.5% here at most in
  instructions.
- The DMA block writer with each command's push/pop/test flag computed once per command instead of per entry:
  `DMA::Run9` +25k instructions a frame (GCC's register shuffling in the loop grew): dropped.
- NEON lighting: MK's normals have ~1 light each; ~60k instructions a frame to gain at most: not done.

## What the next session should know

- **Compare savestates for timing changes.** On MK, a DMA timing error of a few cycles per burst left main RAM, the
  CPUs' registers and the geometry engine's state at every frame end identical (the game waits for VBlank), but the
  savestate differs. `/home/user/perf3/gl/sst.sh <bin> <frame> <abs dir>` (MK scene, `headless.reload_at`).
- `bananads-difftest` can now run a game's scene: `--input <headless.input> --fresh-save --every --from N`;
  `/home/user/perf3/gl/mkdiff.sh` wraps it for MK (A and B with their own save dirs, rtc.fixed). ~2 min under qemu.
- qemu counts of the emu thread vary by 0.3-0.9M a frame with Gengis bins and 2D lines it draws itself
  (`line_px`, `rast_kern_*`): subtract them (`/home/user/perf3/gl/qsum.py`).
- MK's stream: `stream.txt` (texcoord 7.5k, VTX_16 3.8k, normals 4.6k with ~1 light, other vertex formats 4.8k,
  ~1,330 polygons a frame; GXSTAT read 7 times a frame; RAM_COUNT/matrices/tests never).
- Left in the geometry (qemu, per frame): `Run` 2.21M (the loop now ~20 per entry; the rest is the commands and
  `AddCycles`), lighting 0.64M (now out of line), `DMA::Run9` 1.77M (the block writer ~35 per word, ~0.3M per-call
  cost incl. a 256-byte `MRAMBurstTable` copy per call), polygon setup 1.14M, vertices 0.78M (already NEON).
  Each idea left is ~1% or less (a precomputed packed-command schedule in the writer, `MRAMBurstTable` as a pointer,
  NEON lighting). The DMA burst is spread over ~1,260 `Run9` calls a frame because each slice ends it: that cost is
  in the slice loop (mkside's area).
- A lazy engine could still pay on the device through the A55's L1I (geometry in longer runs between JIT code),
  which qemu can't show. If tried: log (slice end, entries written) per `Run` point and replay, and check with
  savestate compares.

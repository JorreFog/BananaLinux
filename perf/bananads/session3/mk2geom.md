# mk2geom: the geometry left on the emulator thread, and session 2's GX store changes (session 3, phase 2, 2026-10-19)

Branch `claude/bananads-perf-mk2geom` on JorreFog/SuperDrastic, from the checkpoint `8e98258`
(`claude/bananads-perf-mk-checkpoint1`). Head **`58aa659`** (0066 added after the coordinator's follow-up; the
first hand-over's head `dc33057` has 0063-0065, CI artifact `bananads-aarch64-dc3305761147359b987218b33a63dc1ecb13801d`).
CI for 58aa659 (`bananads-device`, run 39244768999, passed): artifact **`bananads-aarch64-58aa65959d04f6c295364ef744a17b3d25ae5948`**. **Untested on device.**
melonDS patches 0063-0066 (they apply between 0062 and 0070; mk2cpu's 0072 applies on top of 0063-0065; 0066
touches only DMA.cpp/DMA.h).

## Answers first

- **How much of MK's GX traffic comes from CPU stores?** ~6% of the FIFO's words. Frames 1700-1899
  (`MELONDS_IOSTATS`, `mk2geom-evidence/iostats-mk-1700-1900.txt`), per frame: ~31,700 words by the GX FIFO DMA,
  **2,076 by the ARM9's single STRs to 0x04000400**, 16 by STMs (2 STMs). The device profile's
  `WriteToGXFIFOBody<true>` 6.4% was the DMA's per-word path; phase 1's 0060 already replaced it (2.08M -> 0.15M
  instructions a frame under qemu; the 0.15M left are the STRs).
- **Session 2's memio changes, ported:** 0021 -> **0063** (`render.gx_batch`), 0022 -> **0064** (`render.gx_str`).
  0021 did not apply on 0060 (both rewrote the DMA's block writer and added FIFO accessors), so 0063 reuses 0060's
  block loop for STMs and drops 0021's DMA hook. 0022 applied with two context fixes. Both exact on MK. On MK they
  are worth ~0.1% together (MK sends almost nothing by STM). They are for HeartGold, where STMs into the GX FIFO
  were 5.7% of the thread.
- **The biggest geometry item left by my count:** nothing big. Per-TB counts (`mk2geom-evidence/tbs-geometry.txt`):
  polygon setup 1.14M a frame (3,300 calls, ~340 instructions each, no block over 11%), vertices 0.78M (8,100,
  ~95 each, 40% the NEON transform), lighting 0.64M (~140 per normal), `Run` 2.21M, `DMA::Run9` 1.77M (~37 a word,
  plus ~185 per call over ~1,240 calls a frame). The one compact item with a real cut was **the polygon sort:
  108.7k -> 39.5k (0065, `render.geom_sort`)**. Then, as the coordinator's follow-up, **`DMA::Run9`'s per-call
  cost: -64.4k (0066, `render.gx_dmarun`)**.

## Commits (all toggles default `on`, all exact on every check below)

| Commit | melonDS | Toggle | What | qemu, emu thread, MK 1650-2150 | Expected on device |
|---|---|---|---|---|---|
| 10ee98d | 0063 (session 2's 0021) | `render.gx_batch` | the JIT's STMs into the GX FIFO handed over in one call, through 0060's block loop; adds tests/gxstm | +3.4k (the test on ~210 other STMs a frame) | MK: ~0. HeartGold: session 2's +1..2.5% fps if its STM share holds |
| 53fb2a7 | 0064 (session 2's 0022) | `render.gx_str` | the JIT's STRs to the GX ports before the region switch; a parameter word straight into the FIFO | -15.6k (`WriteToGXFIFOBody` 153.9k -> 103.1k, `GXWrite9Route` -23.6k, `WriteToGXFIFO` +49.4k, `SlowWrite9` +9.4k) | **~-0.1%** |
| dc33057 | 0065 | `render.geom_sort` | VBlank's polygon Y-sort as a stable counting sort (sets of 64+); session 1's dropped patch | **-69.2k** (the sort 108.7k -> 39.5k) | **-0.5..-0.8%** |
| 58aa659 | 0066 | `render.gx_dmarun` | `DMA::Run9` runs the GX FIFO DMA's block path in a function of its own (`Run9GX`) when that path is all its call would do | **-64.4k** in the DMA (`Run9` 1,772.8k -> `Run9GX` 439.0k + `WriteGXFIFOBlock` 1,135.7k + `Run9Full` 57.5k + the check 76.2k); thread -69.7k | **-0.4..-0.6%** |

Together (qemu, `mk2geom-evidence/qcount.txt`): the emulator thread **12.88M -> 12.71M instructions a frame
(-1.3%)** without the Gengis bins it draws itself (12.78M at dc33057).

**Expected device gain, with the reasoning:** the profile has the sort (`__move_merge` + `__insertion_sort`) at 0.9%
of the thread's cycles. 0065 removes ~64% of its instructions, and the ones that go are the merges' data-dependent
compare-and-branch steps (mispredicted about half the time on random keys) plus a malloc/free a frame; the counting
passes are straight loads, increments and stores. So -0.5..-0.8% of the cycles. 0064: the STR path is ~0.27M
instructions a frame (~2% of the thread); it removes ~6% of that: ~-0.1%. 0063: nothing on MK. 0066: ~52 fewer
instructions in each of ~1,240 calls a frame (the profile's `DMA::Run9` 5.3% went mostly into 0060's block writer;
what 0066 removes is setup: loads, compares, spills), and the GX DMA's call now runs a ~340-instruction function
instead of reaching into `Run9`'s ~2,100 between JIT blocks (L1I, 9.9 misses per 1000 instructions in the profile):
-0.4..-0.6%. **About -1.0..-1.5% for the branch on MK's race start** (53.7 fps slowest window -> ~+0.7 fps). No
change in frame pacing or audio.

## Device commands (most useful first)

Binary: the CI artifact above. Base: checkpoint1's artifact `bananads-aarch64-8e98258dd82b4191e932c75572a06f0183fd460b`.
`OFF="--set render.gx_batch=off --set render.gx_str=off --set render.geom_sort=off --set render.gx_dmarun=off"` gives exactly checkpoint1's
paths in this binary.
1. `mk.sh` headless at 1992 MHz, frames 1650-2150 and 1700-2700: checkpoint1 vs mk2geom, and mk2geom with
   `EXTRA="$OFF"`. Report the slowest 100-frame window. (Expect under 1%: two runs each.)
2. The same with `--set prof.hz=1000`: `YSortCounting` should replace `__move_merge` + `__insertion_sort` +
   `__merge_adaptive`; `WriteToGXFIFOBody<true>` should drop by a third; `DMA::Run9` becomes `DMA::Run9GX` +
   `GPU3D::WriteGXFIFOBlock` (their sum is the one to compare). If there's time, `--set render.gx_dmarun=off` alone
   (0066 is the one change here that could gain more or less on the device than its count, through the L1I).
3. `kmsmk.sh` with mk2geom: `kms.stats=60` through the countdown, and the clock the governor settles at.
4. **HeartGold** (where 0063/0064 were meant to pay), session 2's memio commands:
   `CLK=1104000 EXTRA="--set render.scale=2" hgquick.sh ./bananads` vs
   `CLK=1104000 EXTRA="--set render.scale=2 --set render.gx_batch=off --set render.gx_str=off" hgquick.sh ./bananads`.
5. **`exact.sh` on Golden Sun: Dark Dawn and HeartGold**: `exact.sh g2-on <bin>`, `exact.sh g2-off <bin> $OFF`, and
   checkpoint1's binary: the md5 lists must match.

## Exactness run (details: `mk2geom-evidence/exactness.txt`)

0063-0065: all three toggles off vs on, device build under qemu (0066's own run below):
- Mario Kart (mk-scene.sh, fresh save): the 8 frames and the audio dump identical to the base hashes
  (mkside-evidence/mk-scene-base-md5.txt), on and off; main RAM every frame 1600-2100 with both CPUs' registers and
  the geometry timing state (`bananads-difftest --regs --timing`): SAME; **full savestates at 1850 and 2000
  byte-identical** on, off, and to the checkpoint's build (the sorted polygon order is in the savestate).
- gxstm (session 2's ROM, now in tests/gxstm), geomtest, dsstress L5/L10: pictures every 10th of 300 frames (soft,
  Gengis 2x + 2D thread, savestate reload) identical; difftest gxstm, geomtest, geomtest-irq (`--regs --timing`),
  dsstress L5, iorom L5: SAME. jit's `exact-suite.sh`: FAILS=0.
- Sabotage (one build, each sabotage enabled alone by its toggle): 0063 (an STM's push/pop commands not counted):
  gxstm DIFFERENT at frame 1 (MK doesn't catch it: 2 STMs a frame). 0064 (a quick parameter into an empty FIFO put
  in the FIFO, not the pipe): MK's savestate at 1850 differs, gxstm hangs. 0065 (the first counting pass unstable):
  MK's savestate at 1850 differs. geomtest-irq catches neither 0063's nor 0064's (its GX words come by DMA).
- **0066** (`render.gx_dmarun` off vs on, build of 58aa659): MK's frames and audio = base; main RAM every frame
  1600-2100 with registers and geometry timing SAME; **full savestates at 1850 and 2000 byte-identical** to the
  checkpoint's; check.sh's 16 picture sets and difftests and geomtest-irq SAME; exact-suite.sh FAILS=0. A **trace
  build** (every `Run9` call's ARM9 timestamp, source, counts, burst table position and state folded into a hash;
  `mk2geom-evidence/0066-trace-build.patch`, not committed): the same hash over all 676,042 calls of the scene, off
  and on. Sabotage: one cycle more when a block starts at a burst's second unit: trace, savestate at 1850 and audio
  differ (caught). Two other sabotages in `Run9GX` (a DMA's first unit not started as a new burst; a burst
  restarting inside a block) left even the trace identical: MK's GX DMA never takes those two branches (in both
  bodies they are the old statements, unchanged), so nothing here checks them.

## Tried, looked at and dropped (for DEAD-ENDS.md)

- 0021 as written (its own block loop plus a hook in the DMA's block path): superseded by 0060; 0063 shares 0060's
  loop instead.
- 0063's first form let LTO inline `WriteGXFIFOWords` into `SlowBlockTransfer9`, so every non-GX STM paid a
  register-pair frame (+5.2k a frame); now `noinline` (+3.4k).
- Polygon setup: the culling normal's normalising loop almost never iterates on MK; the vertex copy into vertex RAM
  is ~39 instructions a vertex x 2,230 a frame (87k): at most ~20k to gain. Not done.
- Vertex submission: ~95 instructions a vertex, already tight. Lighting: ~140 a normal with one light (phase 1's
  NEON estimate stands). Not done.

## What the next session should know

- **MK's GX words come from the DMA (94%) and single STRs (6%), not STMs.** If the STR path matters on the device
  (`WriteToGXFIFOBody<true>` + `SlowWrite9` + the JIT's slow call: ~0.27M instructions a frame, ~2%), the next step is
  the JIT side: an STR whose base the JIT knows is the GX FIFO port could call a GX-only entry directly (memio's
  idea), and STRs of consecutive parameters could be queued. That is the JIT's IO slow path (mk2cpu's area this
  phase).
- **`DMA::Run9`'s per-call cost** was ~185 instructions x ~1,240 calls a frame; 0066 takes ~52 of them. What is
  left per call (~130, in `Run9GX`, `WriteGXFIFOBlock`'s entry and `Run9End`) is the prologue/epilogue of two calls,
  the timing lambda's setup and the block writer's state load/store. Fewer calls (not ending the DMA's burst at
  every slice end) is the slice loop's business.
- **For DMA timing changes, a per-call trace beats the savestate.** MK's frame-end savestate absorbs a few cycles
  of DMA timing in some cases (the game waits for VBlank); `mk2geom-evidence/0066-trace-build.patch` hashes every
  `Run9` call's state and timestamp, and it also shows which branches the scene never reaches.
- The polygon order is in the savestate, so a savestate compare checks any sort change exactly.
- qemu per-symbol diffs need the LTO suffixes (`.localalias`, `.lto_priv.N`, `.constprop.N`) normalised: they move
  between builds (`/home/user/perf3/mk2g/symdiff.py`).

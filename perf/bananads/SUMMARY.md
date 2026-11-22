# BananaDS speed session, 2026-10-18: results

## Device verdict (RG DS Plus, measured by the local agent after the session): read this first

The local agent tested the integrated series on an RG DS Plus with BananaOS, in the HeartGold overworld (a real save,
600 measured frames). The full write-up is [GUIDE-FOR-CLOUD-AGENTS.md](GUIDE-FOR-CLOUD-AGENTS.md).

| | The simulator said | The device measured |
|---|---|---|
| Everything together, 2x | -2% to -46% per frame | **+3% fps** (105.5 -> 108.6 at 1992 MHz; 69.9 -> 71.4 at 1104 MHz) |
| Everything together, 1x | -6% to -10% | **+2% fps** |
| `render.3d_pipeline` | the emulator's thread -27% | **0%** |
| `render.2d_thread=on` at 1x (already in the base code, off by default) | the emulator's thread -36% | **+21% fps on the base, +26% with the patches** |
| "At 2x the 2D thread is now the limit" | yes | **No: the emulator's thread is 99% busy, the 2D thread 31%, the Gengis workers 25% each** |

All patches were frame-exact on the device. The work was safe, but it was mostly aimed at the wrong thread.

- **Test ROMs are not games.** The test ROMs stress the 2D engine and the renderers. HeartGold, like most real
  games, is bound by the emulator's thread: JIT code, memory and IO slow paths, geometry, the scheduler, the SPU.
- **rgds-sim's thread-level conclusions were wrong for a real game.** That includes the "2D thread is the critical
  path at 2x" finding the session re-planned around at 16:52, so treat every simulated number on this page as
  untested, not as a result.
- **The biggest win of the day was a default nobody had flipped:** `render.2d_thread=on` at 1x.

Next time: start from the device's per-thread CPU times and a profile of the busiest thread on a real game, and work
only on the thread at ~100%. Hand over small batches as a SuperDrastic branch. The guide has the full checklist.

---

The original session summary follows, unchanged. Its numbers are simulator numbers on test ROMs; the device verdict
above supersedes them.

Four cloud agents worked on BananaDS's speed on the RG DS and RG DS Plus for about 3.5 hours (14:58-18:28 UTC), with
no handheld. The work and its proof are in this folder; this page is the summary.

- **Numbers come from a simulator, not a handheld.** Every speed number below is from **rgds-sim**, a Cortex-A55 /
  RK3568 timing model running BananaDS under qemu, on the repository's test ROMs.
- **Builds match the device's toolchain.** All builds use the device's own toolchain and flags: Bootlin GCC 14.3,
  -O2, as BananaLinux's Buildroot builds it.
- **Nothing is measured on a handheld yet.** The local agent decides with [DEVICE-TEST.md](DEVICE-TEST.md).
- **The changes are exact.** Every change keeps the emulation exact (pictures, main RAM and sound identical to the
  start of the session) and has a toggle whose "off" is the old behaviour.

## Before vs after: everything together

The start of the session is BananaDS `00c34c78e8de`, the commit BananaLinux pins. "After" is the integrated series
`patches/integrated/` (23 commits on top of it, SuperDrastic `perf/integrated` @ 4de7beb), built with a test-ROM PGO
profile. Run #2 used the profile trained on the old tree, which is stale for the changed files. Run #3 used the
profile retrained on the integrated tree, `pgo/prof-int-gcc14.3-4de7beb.tar.gz`, which is how it should ship.

The table shows ms per frame of the frame's slowest thread, which is the one that sets the frame rate. Each cell is
at 1104 MHz (at 1992 MHz). Sources: `sim`, `evidence/sim/combined-int2.txt` (#2, 17:46 UTC) and `combined-int3.txt` (#3, retrained profile, 17:53 UTC).

| Workload | Before | After | Change |
|---|---|---|---|
| ds2dtest (2D test ROM) at 2x | 16.49 (9.41) | 11.49 (6.63) | **-30%** |
| membench-win (2D windows over 3D, a game HUD) at 2x | 7.88 | 4.27 | **-46%** |
| dsstress-L5 (3D stress) at 2x | 4.58 (main thread) | 3.96 (2D thread) | **-2% to -14%** (dev-mode noise ±4%) |
| membench (a game frame's memory traffic) at 1x | 4.66 | 4.22 | **-10%** |
| dsstress-L5 at 1x | 6.86 | 6.71 (#2) / **6.46 (#3, retrained profile)** | -2% / **-5.8%** |
| ds2dtest, 2x on one thread (st, deterministic) | 26.40 (15.01) | 19.16 (11.00) (#3) | **-27%** |
| cpubench-thumb (CPU-bound game logic) at 1x | 5.48 | 5.46 | -0.5% |

Exactness of the integrated build: pictures and audio are identical to the base on 17 ROMs at 1x and 5 at 2x. render's
matrix on the integrated head passed 17 configurations with FAILS=0.

How to read it:

- **At 2x (BananaLinux's default) the main thread is no longer the limit.** The 3D pipeline moved the bins it drew at
  VBlank to the Gengis workers: the emulator's thread is 30-40% lighter, and the workers carry 23-27% more.
- **The 2D thread is now the limit at 2x, and it got much faster.** On 2D-heavy and windowed scenes it is 30-46%
  faster. On the 3D stress ROM it is only a few percent faster, because there its cost is reading Gengis's 3D rows
  from memory, which only a smaller 3D format or prefetch can cut.
- **At 1x the gain is moderate.** The 2D engine runs on the emulator's thread at 1x, where the compositor and window
  fixes give -10% on membench. PGO gave -4 to -13% on its own (LEDGER L7). With the old tree's profile it lost most
  of that on the integrated tree: dsstress-L5 1x was -2.3%. Retrained on the integrated tree it is back: -5.8%, with
  L1I misses at 10.9k -> 6.4k per frame. Ship PGO only with a profile from the final tree
  (`bananads/tools/perf/pgo/train.sh`, `pgo/README.md`).
- **One default change could be big at 1x.** Turning on the 2D thread at 1x (`render.2d_thread=on` at
  `render.scale=1`) took 36% (dsstress-L5) and 67% (ds2dtest) off the emulator's thread in the model. It is a device
  A/B, then possibly a one-line default.
- **The model has known limits.** It is calibrated only partly: it is within 1.9% of the one single-threaded device
  number, but the threaded device numbers can't pin its costs. A/B deltas on one workload are more trustworthy than
  absolute ms. It can't see OS scheduling or contention between the 5+ threads on 4 cores, and the test ROMs aren't
  games.

## What is in the patches

`patches/integrated/` applies with `git am` onto `00c34c78e8de` and reproduces the tested tree exactly (checked: same
git tree hash). Each line below is one change, its toggle, its default, and its gain in the model where measured.

**The renderers and the threads** (render; `findings/render.md`):

| Change | Toggle | Default | Gain in the model |
|---|---|---|---|
| Gengis frames overlap; VBlank waits for conversion only | `render.3d_pipeline` | on | the emulator's thread -27% at 2x (VERIFIED) |
| 2D compositor loop rewritten for the device's -O2 | `render.2d_comp_unroll` | on | -5..-6% at 1x (VERIFIED) |
| the four 3D quarters share one 2D pass at 2x | `render.2d_quarters` | on | the 2D thread's composite -28% |
| output stage writes the 2x rows directly | `render.2d_emit_zip` | on | the 2D thread -7% |
| 2D job ring 128 KB instead of 2.5 MB | `render.2d_job_stride` | compact | dTLB walks -62% |
| windows and the 3D layer vectorized | `render.2d_win_neon` | on | the 2D thread -47% on a HUD scene |
| compositor step list cached | `render.2d_steps_cache` | on | -0.3..-1.6% |
| 3D line sharing at 1x | `render.3d_share`, `render.3d_wait2d` | on | -18% at 1x with the 2D thread |
| waits for the 2D thread block instead of spinning | `render.2d_fence_wait` | block | device only |
| 2D thread batch size | `render.2d_batch` | 16 | device only |
| 3D-row prefetch | `render.2d_prefetch` | **off** | device only |
| bitmap BG conversion 16 px a NEON step (jit) | `render.2d_conv16` | on | -1.9% at 1x |
| prefetch of the bitmap BG's VRAM rows (jit) | `render.2d_prefetch16` | on | -2% (model v3: assumption) |

**The JIT** (jit; melonDS patches 0021-0024; `findings/jit.md`):

| Change | Toggle | Default | Gain in the model |
|---|---|---|---|
| JIT code on 2 MB huge pages | `jit.hugepages` | on | -0.4..-0.7% |
| masked IRQs don't end block chains | `jit.irqchain` | on | -16.9% on a masked-IRQ stress ROM; ~0 elsewhere; HeartGold's rate unknown |
| no entry prologue per block | `jit.entry_x26` | on | -1.7% on a game-sized code ROM |
| why block chains return | `MELONDS_JIT_EXITSTATS=n` | - | a diagnostic |
| new test ROMs | - | - | tests/irqmask, tests/bigcode |

**The build** (core; `findings/core.md`). All are CMake options, all off by default, chosen per build:

| Option | Gain in the model |
|---|---|
| `BANANADS_PGO=<profile>` (+ `BANANADS_PGO_GENERATE`, `bananads/tools/perf/pgo/train.sh`) | -4..-13% on the base tree (VERIFIED); stale-profile behaviour documented |
| `BANANADS_ALIGN=32:16:16` | -0.1..-3.9% on top of PGO on the base tree, but only -0.3% on the integrated tree (sim 17:49): the earlier -3.6% was layout luck; device only |
| `BANANADS_HOT_OPT=-O3` | small; slower combined with PGO: not recommended |
| `BANANADS_HOTCOLD`, `BANANADS_NOPLT` | dead ends, kept as options |

Not in the integrated series, each a separate series on top:

- `patches/core-jitmem/` (melonDS 0041, `jit.gfxstores`): the JIT's VRAM/OAM/palette stores through JIT-emitted
  helpers, -0.7..-1.1%. It applies cleanly with `git am` on top of `patches/integrated/` and builds with the device
  toolchain. Exactness on that combination, toggle off vs on: jit's full suite gives 26 of 26 pictures and difftest
  checks identical and `bananads-tests` passes, ds2dtest's pictures and audio are identical (`evidence/coordinator/`), and core's sabotage build
  was caught 12 of 12. It was not in the combined speed measurement.
- `patches/core/` 0040 `core.fast_div` (-0.4%, off).

## Proof and process

- [LEDGER.md](LEDGER.md) lists every result that a second agent re-measured. VERIFIED means the result was re-measured
  by `sim`, not by its author. PENDING rows are listed as such.
- [BOARD.md](BOARD.md) is the full bulletin board: every claim, result, dead end and hand-over, with times.
- [DEAD-ENDS.md](DEAD-ENDS.md) lists what earlier sessions ruled out and what this one tried and dropped (-fno-plt,
  hot/cold splitting, -O3 with PGO, a counting sort for polygons, inline graphics stores at every site, a lighter
  ARM_Dispatch, and others).
- `evidence/<agent>/` holds the raw output behind every number. `findings/<agent>.md` holds each agent's write-up,
  with the device A/B commands for its changes.
- [rgds-sim/](rgds-sim/README.md) is the simulator: a qemu TCG plugin with A55 caches, TLBs, branch prediction,
  write streaming and prefetch. It comes with `run.sh`, `ab.sh`, `regions.py` (cache misses per buffer) and
  `calib/device-calib.sh`, which refits the model from the handheld's own numbers.
- The device's build is reproduced off the device by `/home/user/perf/tools/build-a64-device.sh`, documented in
  findings/core.md. The first finding of the session was that the reference build everyone measured at first was not
  the device's (GCC 13 -O3 with stack canaries vs GCC 14.3 -O2).

## What's next

- **Device A/Bs:** run the plan in DEVICE-TEST.md, first `render.3d_pipeline` and `render.2d_thread` at 1x, then PGO.
- **PGO:** retrain the profile on the final tree, then collect a profile from real games on the handheld.
- **The 2D thread at 2x:** a smaller 3D pixel format (the 768 KB/frame read of Gengis's rows), display capture on the
  2D thread, rotscale sprites and mosaic.
- **2D layer costs on ds2dtest:** DrawBG_Affine 4.4 ms, DrawBG_Text 4.2 ms, sprites ~3.9 ms per frame (jit's
  attribution).
- **JIT:** a cycle budget in a register (analysed in findings/jit.md, about 6% of cpubench), and the JIT's code
  footprint. On a game-sized footprint the JIT's code is fetch-bound, at ~0.25 instructions per cycle.

## How the session ran

- **Start and agents (14:58 UTC).** The session started with four agents: `sim`, the simulator and verifier, and
  `jit` on Opus 5.5; `core`, the build and C++ core, and `render`, the renderers and threads, on Fable 5.1 at the
  user's request. One coordinator ran the board, the ledger and the integration.
- **16:39 restart.** The first `core` run was ended by the harness after its hand-over; a second run took over from
  its status file.
- **16:52 re-plan.** sim found that at 2x the 2D thread is the frame's limit. At the user's request jit, render and
  sim moved to the 2D engine (2D-PLAN.md), and core moved to the JIT's inline graphics stores.
- **17:29 deadline.** The user set the end at 18:28 UTC. The integrated series, the combined measurements and these
  documents were made in the last hour.
- **What guarded against hallucination and circles:**
  - every number has an evidence file;
  - each result is re-measured by another agent (LEDGER);
  - ideas were time-boxed, and DEAD-ENDS.md was read before claiming one;
  - wrong timestamps and a misquoted number were caught and corrected on the board.
- **Weekly usage.** The 45%-of-weekly-usage stop condition couldn't be read from inside the session; the clock was
  the limit.

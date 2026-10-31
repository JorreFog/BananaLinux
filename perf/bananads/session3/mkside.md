# mkside: the emulator thread's other costs in Mario Kart DS's race start (session 3, 2026-10-18)

Branch **`claude/bananads-perf-mkside`** on JorreFog/SuperDrastic, from `claude/bananads-perf-mkbase` (9aa478c).
Head **`db1eb04`**. CI (`bananads-device`): artifact **`bananads-aarch64-db1eb04fdb25424aeb235a5c6996a0aec491f39b`**
(checked here: the artifact's binary runs the scene under qemu with frames and audio equal to the base, toggles on
and off). **Untested on device.**

melonDS patches: 0030 and 0031 (session 2's `loop` branch, cherry-picked unchanged), 0070, 0071 (mine). No other
session 2 branch needed. geomlazy test-merged its head 1cd2e36 with this branch at e855c82: no conflicts, exact
(`/home/user/perf3/gl/merge`, local).

## The scene script (for everyone)

`mkside-evidence/mk-scene.sh <bananads> <out dir> [--set ...]` (master copy `/home/user/perf3/mk-scene.sh`): a fresh
save each run (name and emblem dialogs answered by A taps every 20 frames from 120, A held from 1400), Grand Prix,
50cc, Mario, the first kart, Mushroom Cup, **Figure-8 Circuit**. 2100 frames, ~80 s under qemu.

| Frames | What |
|---|---|
| 0-1290 | menus (nickname "BananaDS" confirmed, emblem skipped) |
| 1420-1535 | course intro flyover |
| 1540-1675 | loading (black) |
| 1680-1775 | grid, before the countdown |
| 1780-1965 | countdown ("3" 1780, "2" 1840, "1" 1900, START ~1965; the kart spins out: A held) |
| 1965- | race |

Dumps kept: 1500, 1740 (before), 1800/1860/1920 (inside), 1980, 2040/2100 (after), plus `audio.raw`. It fails with
exit 2 if frame1860 isn't the base build's (`58b1524b...`) and exit 3 if the audio isn't (`0266610c...`).
Note: the brief's `rtc.fixed=on` isn't parsed (it needs a date), so that command ran on the real clock; the
script uses `2026-10-18 12:00:00`. The fresh-save dialogs shift the timeline ~30-100 frames from the user's save:
the device's 1650-2150 window is **not** the same frames as the script's 1680-2100.

## What the costs are (counted in the scene, frames 1680-2100, per frame)

- **`main::{lambda()#1}` (8.0%)** is `run_headless` with, through LTO, `RunFrame` -> `RunSlicesJIT` (the slice
  loop), `RunEventsDue` (the scheduler), `ExecuteJITInline` (block lookup and dispatch), `HaltInterrupted`, the GX
  stall/DMA branch of the loop and the headless frontend's audio drain (read from a `-g1` build's line table; its
  code is byte-identical to the device build's). Split (qemu, 1131k instructions): slice loop 44%, scheduler 12%,
  ARM9 dispatch 12%, `HaltInterrupted` 7%, `IsEmpty` (GX idle check) 5%, ARM7 dispatch 4%, `DMA::Run` 4%
  (`mkside-evidence/frame-loop-breakdown-*.txt`). Both CPUs are halted for much of the frame (537 halt skips a
  frame), so session 2's `loop` branch applies: **brought in** (below).
- **SPU (7.7%)**: 547 Mixes a frame (the event's period, 1024 cycles, is the output rate: not changeable exactly),
  each running ~10.4 channels: ~4.6k ADPCM calls taking 6.1k samples and 1.1k PCM16 calls a frame; only 3.8 Mixes
  a frame follow a sound register write, so FastMix's "only due channels" doesn't help: every channel is due every
  Mix. Per channel per Mix: ~75 instructions in the Mix loop and ~110 in `RunValid`.
- **Non-GX DMA: nothing.** DMA3 in GX FIFO mode: 70 starts and 31.6k words a frame (geomlazy's); the only other DMA
  is 4 immediate DMA3 starts, 906 words. `DMA::Run9` is the geometry's.
- **Scheduler (9.3%)**: mostly the frame loop above. Events a frame: LCD 526, SPU 547, RTC 548 (32768 Hz tick),
  Div 162, Sqrt 85, SPI 12. Removing or merging events moves slice ends (not exact); `RunEventsDue` is ~75
  instructions an event.
- **`pthread_cond_signal` (0.9%)**: the emulator thread waking the 2D thread (`BananaRenderer::Kick`'s
  `notify_one`): every 16 of a frame's 383 jobs (192 lines + 191 sprite lines), at VBlank and at the swap; the 2D
  thread is 30% busy, so nearly every kick is a futex wake. (`pthread_cond_broadcast` 0.17%: Gengis's frame start.)
- Also found: **code region setups** (`SetupExecutableRegion`, `GetMirrorLocation`, `ClassifyAddress9`: ~1.1%):
  the ARM9 changes code region ~1500 times a frame (ITCM <-> main RAM), the ARM7 ~370 (BIOS <-> WRAM).

## Commits (all toggles default `on`; each `off` is the old code)

| # | Commit | Toggle | What | qemu (MK 1680-2100, a frame) | Expected on device |
|---|---|---|---|---|---|
| 1 | 8613622 + 644878f (melonDS 0030, 0031; session 2) | `jit.slice_chain`, `core.halt_skip` | ARM9 slice ends inside the block chain; both CPUs halted -> straight to the next event | frame loop 1151k -> 858k, `SliceChain9` +158k, JIT/stubs -66k: ~-200k | **+1..2.5%** |
| 2 | c26e2cb (melonDS 0070) | `core.fast_samples` | a due PCM16/ADPCM channel's samples in registers (leaf, tables), anything unusual handed to the original loop | `RunValid<1>+<2>` 570k -> 435k | **+0.8%** |
| 3 | e855c82 | `render.2d_kick` (64; old: 16) | the 2D thread woken every 64 jobs | kicks that found it asleep 13/frame -> 3.9/frame (qemu; ~24 -> ~6 on the device) | **+0.6%** |
| 4 | db1eb04 (melonDS 0071) | `jit.region_pair` | a CPU leaving its code region swaps back to the region it last left | region setup 205k -> 122k | **+0.4%** |

The whole branch, qemu, emulator thread without the rendering it does itself: **15.99M -> 15.63M instructions a
frame (-2.3%)** (`mkside-evidence/qcount-branch-vs-base.txt`). Expected together on the device: **+2.5..4%** of the
emulator thread's cycles in the race start, on top of geomlazy's ~8.5%.

Reasoning per row: 1. the frame loop is 8.0% of the thread; its instructions drop ~10% net, and slice_chain removes
`ARM_Ret`/`ARM_Dispatch` round trips (a mispredicted return and ~24 register loads/stores each) that qemu doesn't
weigh: a quarter to a third of the 8% is the optimistic end. 2. `RunValid<1>`+`<2>` are 3.5% of the thread; -24%
instructions, loads/stores of L1-resident state. 3. most of `pthread_cond_signal`'s 0.9% is the futex wake; 4x
fewer wakes. 4. the setup functions are ~1.1%; -40%.
**Frame pacing:** only #3 can touch it: the 2D thread now draws a frame's last ≤32 lines after VBlank instead of
≤8 (~1 ms of its work; `render.2d_lazy` already lets it finish during the next frame). Watch `kms.stats` repeats
with and without `--set render.2d_kick=16`. No audio-path change (the rate control is untouched).

## Device commands (most useful first)

Binary: the CI artifact above. Base: the mkbase build the brief used. All toggles off =
`OFF="--set jit.slice_chain=off --set core.halt_skip=off --set core.fast_samples=off --set render.2d_kick=16 --set jit.region_pair=off"`.
1. `mk.sh` headless at 1992 MHz, frames 1650-2150 and 1700-2700: base vs mkside, and mkside with `EXTRA="$OFF"`
   (the same binary with the old paths: separates the code from the layout). Report the slowest 100-frame window.
   If there's time, one toggle off at a time, `--set core.halt_skip=off --set jit.slice_chain=off` first.
2. The same with `--set prof.hz=1000` (the branch has mkbase's profiler): `main::{lambda()#1}` (or
   `run_headless`, if the inlining changed), `RunValid<2>`/`RunSamples<2>`, `pthread_cond_signal`,
   `SetupExecutableRegion`+`GetMirrorLocation` are the symbols to compare.
3. `kmsmk.sh` with mkside: `kms.stats=60` windows through the countdown (repeats/drops: `render.2d_kick`), and the
   clock the governor settles at after the countdown.
4. **`exact.sh` on Golden Sun: Dark Dawn and HeartGold**: `exact.sh mks-on <bin>`, `exact.sh mks-off <bin> $OFF`,
   and the base binary: the md5 lists must match (Golden Sun is the timing-sensitive one; 0030/0031 change the
   slice loop's code paths, not its slice ends).
5. Optional: `MELONDS_HALTSKIP_STATS=1 <bin> --frontend headless <mk.sh's settings> 2>&1 | grep halt_skip`
   (skips per frame in the race start; 537 here).

## Exactness run (all under qemu, device build)

- MK scene (`mk-scene.sh`): each change off vs on, and everything on, and everything off: the 8 frames and the
  audio dump identical to the base build (mkbase 9aa478c and 00c34c78).
- **MK full savestates** (`headless.reload_at`, geomlazy's `sst.sh`): everything on, frames 1850 and 2000:
  byte-identical to the base (`a57737e0...`, `f476c39e...`).
- MK main RAM every frame 1500-2100 (`bananads-difftest --input --fresh-save --every --from 1500`), each toggle
  off vs on: `core.fast_samples`, `core.halt_skip`, `jit.slice_chain` identical; `jit.region_pair` also with
  `--regs --timing` (both CPUs' registers, GPU3D timing state) identical.
- jit's `exact-suite.sh` (17 ROMs' pictures, 9 difftests, `bananads-tests`), plus sndbench pictures+audio and
  sndbench/dmasnd/ipcrom difftests: `core.fast_samples` FAILS=0; `render.2d_kick` 16 vs 64 with
  `render.2d_thread=on` on both sides plus ds2dtest and geomtest pictures FAILS=0; `jit.region_pair` FAILS=0.
  Session 2's suite runs for 0030/0031 stand (`session2/loop-evidence/`).
- Sabotage: 0070 (ADPCM loop-start state not saved): MK's audio dump differs from the base (what mk-scene.sh now checks, exit 3). 2d_kick (VBlank
  and swap kicks dropped): MK hangs at frame ~240. 0071 (the kept mirror's start off by a word): MK hangs at boot.
  Not caught, by design: 0071 with a wrong region *size* passes every check, because the JIT's lookup entries are
  tagged with the block's address: a wrong region only misses and sets up again (speed, not results).
- Evidence: `mkside-evidence/` (suite outputs, counts, the scene's md5s).

## What the next session should know

- **What's left on the emulator thread beside the geometry and the JIT code**, by device share: the SPU's Mix loop
  (3.7%: ~75 instructions per channel per Mix of bookkeeping: FastLazy/ChanDue, pan, the call; inlining
  `RunSamples` into it gave only -5% under qemu, not committed), the frame loop that remains (~6%: the per-slice
  checks, `RunEventsDue` ~75 instructions an event over ~1,900 events), `blip_add_delta` 0.7%,
  `BananaRenderer::Submit` 0.54% (likely its store-release waiting for the job's stores: batching the `Head`
  publication like the kicks might help; unmeasured), the ARM7's WRAM stores through the slow path
  (`ARM7Write32` 0.76% + `SlowBlockTransfer7` 0.23%: ~1,100 a frame, probably code pages of the sound driver).
- The JIT's region changes still cost the exit to the loop and the re-dispatch even with 0071; a JIT-level
  "call into ITCM and back" without leaving the block chain is the bigger version of it (jit's area).
- Instruction counts under qemu vary by 0.3-0.9M a frame with the Gengis bins and 2D lines the emulator thread draws
  itself: compare per symbol, without `line_px`/`rast_*`/`gx_*` (my `cmp.py`, geomlazy's `qsum.py`).
- To attribute an LTO-inlined symbol: build with `-g1` (same code bytes, checked with `objcopy -j .text`), split the
  symbol's TBs in the qemu plugin (`/home/user/perf3/mkside-work/qc/jc2.c`, `split=<mangled substrings>`), map with
  `addr2line -i` (`an.py`).
- Compare full savestates for timing changes (geomlazy's finding); the MK difftest with `--regs --timing` and the
  audio dump are the other two strong checks.

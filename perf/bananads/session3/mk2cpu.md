# mk2cpu: the emulator thread's costs beside the geometry, after the restart (session 3 phase 2, 2026-10-19)

Branch **`claude/bananads-perf-mk2cpu`** on JorreFog/SuperDrastic, from the checkpoint `8e98258`. Head **`08d14fa`**.
CI (`bananads-device`) passed on the head: artifact **`bananads-aarch64-08d14fadbe164f049dc4843a0c29919abc8c4cec`**. **Untested on device.**

melonDS patch: **0072** (0073-0079 unused). This branch merges with `mk2geom`'s: 0072 applies both on 0001-0071 alone
and with mk2geom's 0063-0065 applied first (`git apply --check`). Its `SlowPathFlags` bit is 7, clear of their bits 8
and 9. Shared files: `emu.cpp` (different blocks) and `docs/PERF.md` (my section sits just before "## Levers not
pulled yet").

## What the costs are at the checkpoint (MK scene, qemu, frames 1680-2100, per frame; 13.56M instructions)

`mk2cpu-evidence/qcount-checkpoint-top45.txt`.

| Area (my counts) | Instructions | What's in it |
|---|---|---|
| SPU | ~1.14M | its event 582k (`MixChannelsFast` ~83 a channel a Mix, ~10.4 channels, 547 Mixes); `RunSamples<2>` 341k (74 a call, 1.34 samples); capture `Run` 43k + `FIFO_FlushData` 29k; `blip_add_delta` 65k (already NEON, as GCC vectorises it) |
| frame loop | 801k | `run_headless` with the slice loop (~60 a slice, ~5,500 ARM9 slices) and `RunEventsDue` (~75 an event, ~1,900 events) inlined |
| ARM7's slow accesses to its WRAM | ~245k | `ARM7Write32` 141k, `SlowBlockTransfer7` 54k, `ARM7Read32` 30k, `ARM7Write16` 16k |
| events | small | `ScheduleEvent` 34k, `CheckDMAs` 34k, `SqrtDone` 32k (370 a call: the bit loop), `StartDiv` 30k, RTC 24k, LCD 23k + `StartScanline` 29k |
| interpreted MCRs (ARM9) | 55k | `A_MCR` 32k + `CP15Write` 24k (the JIT's interpreter fallback; jit's area) |

- **IO stats** (MK, frames 1700-1899): mk2geom's `session3/mk2geom-evidence/iostats-mk-1700-1900.txt`. Beside the GX
  ports (mk2geom's), the big item was the ARM7 writing its own WRAM through the slow path: ~1,040 stores, ~450 STMs
  (2,000 words), ~170 loads and ~60 LDMs a frame. The ARM9's DMA-register polls (~290 a frame), divider and sqrt
  (~330) and IF writes (81) are small, and most of them are already routed.
- **The RTC event, and why it fires every frame:** it is the RTC's 32,768 Hz tick (548 a frame, ~1023 cycles apart),
  next to the SPU's 1024-cycle Mix. Each one ends a slice, so removing or merging it moves the slices' ends and the
  ARM9/ARM7 interleaving: not exact. Its handler is ~44 instructions plus `ScheduleEvent`. There's nothing to win
  exactly beyond a few k instructions.
- **SPU: every channel is due in every Mix in MK.** Its samples come faster than the Mixes, so the event's cost is
  the per-channel state loads, stores and bookkeeping. Two exact attempts didn't pay off (dead ends, below).

## Commits

| # | Commit | Toggle (default) | What | qemu (MK 1680-2100, a frame) | Expected on device |
|---|---|---|---|---|---|
| 1 | 6690b5d + 2edd894 (melonDS 0072) | `jit.wram7fast` (`on`; bit 8 of `MELONDS_MEMFAST`) | on the DS, the JIT's slow loads, stores, LDMs and STMs to the ARM7's WRAM (0x03800000) do `ARM7Read*`/`ARM7Write*`'s case 0x038 themselves: the same `CheckAndInvalidate<1, WRAM7>` and store, without the virtual call and the region switch. A block at 0x038 goes word by word with the code index loaded once | `ARM7Write32/16/8` + `ARM7Read32` -181k, the slow-path entries +19k: **-162k (-1.2%)** | **+0.6..0.8%** |
| 2 | 97bcf49 (no melonDS patch) | `render.2d_ring` (`split`; old: `shared`) | the 2D job ring's counters on cache lines of their own: Head (the emulator's thread writes it every job) on one, Tail and the copy log's `LogRead` (the 2D thread's) on another. Before, Head, Tail, `Kicked`, `FenceCur` and the mutex shared one line. The emulator's thread now reads Tail and `LogRead` again only when the ring or the log looks full by the values it last read | none (qemu doesn't model caches) | **+0.3..0.5%** |

Reasoning:
1. On the device, `ARM7Write32` is 0.76%, `SlowBlockTransfer7` 0.23% and `ARM7Read32` 0.24% (part of that is BIOS
   reads, which this doesn't touch): about 1.1-1.2% of the thread. The path loses ~2/3 of its instructions. These
   are calls, stores and loads to L1-resident state, nothing that runs better than its count.
2. On the device, `BananaRenderer::Submit` is 0.54% for 383 jobs a frame. That is ~420 cycles for ~10
   instructions, which only a cache line moving between cores explains: its release store to Head on the line the
   2D thread had just written Tail to. `NewJob`'s Tail load and `LogReserve`'s `LogRead` load missed the same way, on
   every job and on every copy. With split lines, the job preparation's misses go away and Submit's store no longer
   fights Tail's writes. Expect about half of Submit plus a miss a job. This is the least certain number here: only
   the device can show it.

The third commit (08d14fa) is a test-only setting, `render.2d_test_delay` (default 0): it slows the 2D thread so
the checks reach the job ring's full path (below).

Together: **+0.9..1.3%** of the emulator thread's cycles in the race start, on top of the checkpoint.

## Device commands (most useful first)

Binary: the CI artifact above. A/B within one binary (the same code layout):
`OFF="--set jit.wram7fast=off --set render.2d_ring=shared"`.
1. `mk.sh` headless at 1992 MHz, frames 1650-2150 and 1700-2700: checkpoint1 (8e98258's artifact) vs this branch,
   and this branch with `EXTRA="$OFF"`. Then one toggle off at a time: `--set jit.wram7fast=off`, then
   `--set render.2d_ring=shared`. Report the slowest 100-frame window.
2. The same with `--set prof.hz=1000`. The symbols to compare: `ARM7Write32`, `SlowWrite7`, `SlowBlockTransfer7`
   and `ARM7Read32` for #1; `BananaRenderer::Submit`, `DrawScanline`, `PrepareScanline` and `PrepareSprites` for #2.
3. `kmsmk.sh` with this branch: `kms.stats=60` windows through the countdown. #2 changes when the 2D thread sees
   work, not which work, but watch repeats and drops with and without `--set render.2d_ring=shared`.
4. For the device's own check of #2's full ring, if wanted: `exact.sh` with `--set render.2d_test_delay=150`, with
   and without `--set render.2d_ring=shared`. The md5 lists must match. It runs slower.
5. **`exact.sh` on Golden Sun: Dark Dawn and HeartGold**: `exact.sh mk2cpu-on <bin>`, `exact.sh mk2cpu-off <bin> $OFF`,
   and the checkpoint's binary. The md5 lists must match. Both games' sound drivers run from the ARM7's WRAM, so #1
   is exercised there too.

## Exactness run (all under qemu, device build; evidence in `mk2cpu-evidence/`)

- **MK scene** (`mk-scene.sh`, 8 frames and the audio dump): the checkpoint's hashes with #1 on, with #1 off, with
  both on, and with `render.2d_ring=shared`. Every 10th frame (210) is identical between `split` and the
  ring-bound sabotage build (`mk-scene-runs.txt`).
- **MK full savestates** at frames 1850 and 2000 (`headless.reload_at`, `gl/sst.sh`): the head with defaults is
  byte-identical to the checkpoint (`a57737e0…`, `f476c39e…`; `savestates.txt`).
- **MK main RAM and both CPUs' registers** every frame 1500-2100, `jit.wram7fast` off vs on
  (`bananads-difftest --regs --fresh-save --input`): SAME, for the first form and the final one.
  #2 doesn't touch emulation state (the 2D thread only draws), so no difftest for it.
- **exact-suite.sh** (jit's 17 ROMs' pictures, 12 difftests incl. sndbench, dmasnd and ipcrom pictures, audio
  and main RAM, `bananads-tests`): `jit.wram7fast` off vs on **FAILS=0**. `render.2d_ring` shared vs split, with
  `render.2d_thread=on` on both sides, plus ds2dtest and geomtest pictures: **FAILS=0**.
- **Sabotage:**
  - #1, an STM's last word stored a word late: MK's countdown frame differs. The same binary with
    `jit.wram7fast=off` matches the base, so the toggle really switches the path. This was on #1's first form; the
    final form only hoists the code-index load.
  - #2, the 2D thread publishing Tail on the old line: MK hangs before its first dump.
  - #2, a ring bound 128 jobs too large (undrawn jobs overwritten): **caught**, but only with the 2D thread slowed.
    Under qemu the 2D thread normally keeps up, so the ring rarely fills, and without a slowed thread this sabotage
    changed none of 210 MK frames. `render.2d_test_delay=150` (tests only, commit 08d14fa) makes the 2D thread sleep
    150 us after each job. The MK scene then waits on a full ring ~330k times in 2100 frames. With it:
    - `split` and `shared` give the same 210 MK frames (every 10th), the same audio, and the same 30 ds2dtest frames
      as each other and as runs without the delay;
    - the sabotage changes 28 of the 210 MK frames and 15 of ds2dtest's 30.
    See `2d_ring-slow-2d-thread.txt`. The copy log's full path (the cached `LogRead`) is not shown to be reached:
    the ring fills first.
  - Head 08d14fa, defaults: MK scene frames and audio identical to the checkpoint again.
- A clean build of `2edd894` (pristine melonDS, patches applied by CMake) is byte-identical to the binary most of the
  above ran on. 08d14fa adds only the test setting: one branch per job in the 2D thread, and a settings read.

## Dead ends (also appended to DEAD-ENDS.md)

- **MixChannelsFast's work for a due PCM16/ADPCM channel in one function per format, with `RunSamples` inline**
  (owed cycles, samples, output, pan, next due Mix): exact, but **+112k** instructions a frame. The loop got cheaper
  (582k -> 314k), but the per-format functions cost 760k against `RunSamples`'s 380k: a frame of five register
  pairs and the bookkeeping moved in. mkside's "inline RunSamples into the loop" was -5%. The per-channel state
  traffic is the cost, not the call.
- **`core.fast_due`: a channel due again in the next Mix not marked lazy** (it is owed nothing): exact, -15.6k a
  frame (-2.7% of the event). Too small for a commit.
- `blip_add_delta` is already vectorised by GCC 14 at -O2 (61 instructions): nothing to gain there by hand.
- `RunSamples` with a precomputed safe sample count: not built. At 1.34 samples a call, the setup costs what the
  per-sample checks save.

## What the next session should know

- After #1, what remains of the emulator thread beside geometry and JIT code is mostly the SPU's per-channel state
  traffic (~1M instructions a frame in MK) and the slice loop (~0.8M). Both are near what they can be without
  changing timing or batching the Mix, which is dead. The device's IPC there is ~0.5, with the SPU at ~2,000 cycles
  a Mix for ~1,064 instructions. If the next device profile still shows the SPU at ~6%, try a compact,
  cache-line-ordered channel state (the hot fields of all 16 channels together). qemu can't rank that.
- The interpreted MCRs (`A_MCR` + `CP15Write`, 55k a frame) are the JIT's fallback for the ARM9's CP15 cache
  operations. They're for the JIT agents, if the device shows them.
- Device numbers for #2 decide whether cross-core cache traffic matters on this thread. If Submit drops from 0.54%,
  look at the job slots too: a job's first and last lines share cache lines with its neighbours.

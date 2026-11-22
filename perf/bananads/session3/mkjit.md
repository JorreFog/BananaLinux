# mkjit: session 2's JIT footprint patches on Mario Kart DS's race start (session 3, 22:35-00:00 UTC)

Branch **`claude/bananads-perf-mkjit`**, head **`f1553af`** (`f1553af70273de9ffab43238612acba0ee789691`), from
`claude/bananads-perf-mkbase` (9aa478c), pushed. CI `bananads-device` passed: artifact
**`bananads-aarch64-f1553af70273de9ffab43238612acba0ee789691`** (run 39177061327). Everything below is
**untested on device**. The job was porting and checking jitcode's patches. Nothing new was invented.

| Commit | Patch | Toggle | Default |
|---|---|---|---|
| 4e49c65 | 0040 | `jit.entry_x26`: ARM_Dispatch sets x26, blocks have no 12-byte entry prologue | on |
| 27d2b99 | 0041 | `jit.dyncold`: a register return's cached exit keeps its miss way in the far region | on |
| 170cd7d | 0042 | `jit.flagstub`: an exit's stub puts the pending flags in the CPSR | on in the patch |
| f1553af | (docs, emu.cpp) | **`jit.flagstub` defaults to off** in BananaDS; PERF.md has the MK section | off |

The first three are jitcode's cc73062/bf60358/863d297 cherry-picked unchanged (jitcode's docs commit 96b339f is
folded into f1553af's PERF.md text).

## Code size on the MK scene (a static fact, not a speed result)

`MELONDS_JIT_PERFMAP=1` + `census.py` over every block of the scene (mk-scene.sh, 2100 frames). Each build compiles
the same 14,415 blocks. The frames and audio of every build match mkbase.

| Build | Block bytes | Bytes/block | Instructions |
|---|---|---|---|
| all three off | 4,371,356 | 303 | 1,092,839 |
| + 0040 | 4,198,376 (-4.0%) | 291 | 1,049,594 |
| + 0041 (= **the branch's default**) | 4,093,128 (-6.4%) | 284 | 1,023,282 |
| + 0042 (`jit.flagstub=on`) | 3,934,836 (-10.0%) | 273 | 983,709 |

The far region grows by 131,568 bytes (0041). The blocks reach 0042's stubs from 13,191 exits: ExitCheckF 5,920,
ExitCheckPCF 7,271 (from a histogram of BL targets, `bl-targets-mk-on.txt`). The bytes come off the same
categories as on bigcode: constants (the x26 prologue), flags/CPSR (-40%) and conditional branches.

## Expected gain on the race start, and why

Only instruction fetch can change. The instructions that run are the same, apart from 3 fewer per ARM_Dispatch
(0040; dispatch and exits are 1.2% of the thread) and one extra B on a return-cache miss (0041). The profile's PMU
line gives:
- 145.6k L1I misses a frame. At ~10-12 cycles each (an L2 hit) that is ~5-6% of the frame's 29.95M cycles.
- 61k iTLB misses a frame, ~1-2% more.

So instruction-fetch stalls are ~6-8% of the emulator thread. The JIT's code (29.1% of samples) holds somewhere
between half and all of those misses. If its misses drop in proportion to its bytes:

| Change | Bytes | Expected emulator-thread gain |
|---|---|---|
| 0040 `jit.entry_x26` | -4.0% | +0.1-0.3% |
| 0041 `jit.dyncold` | -2.4% | +0.1-0.2% |
| 0042 `jit.flagstub` (off by default) | -3.6% more | +0.1-0.3% |
| branch default (0040+0041) | -6.4% | **+0.2-0.5%** |
| with `jit.flagstub=on` | -10.0% | +0.3-0.8% |

This is an upper-bound style estimate. Capacity misses don't fall linearly with bytes, and most of the 4.4 MB
compiled isn't hot. It is well below the device's run-to-run noise and far from the brief's -15%. jitcode's ~+2%
came from rgds-sim (a model, DEAD-ENDS). I'd trust the PMU arithmetic more. The device can confirm the mechanism
only with `perf stat -e L1-icache-load-misses,iTLB-load-misses`, or `prof.pmu`, comparing all toggles off against
on. **If the device shows nothing, stop JIT footprint work for MK.** The JIT's 29% is then ALU and guest work.

## Device commands (the brief's section 5)

Binary: the artifact above. Base: the mkbase build (or 0.4.0's 00c34c78). The off settings give exactly the old
code:
`OFF="--set jit.entry_x26=off --set jit.dyncold=off --set jit.flagstub=off"`
1. `mk.sh` headless at 1992 MHz, frames 1650-2150 and 1700-2700: base vs mkjit (default), and mkjit with
   `EXTRA="$OFF"` (same binary, old code: this isolates the footprint from the build). Report the slowest
   100-frame window. Optionally a third run of mkjit with `EXTRA="--set jit.flagstub=on"`.
2. The same with `--set prof.hz=1000` (the branch has mkbase's profiler). Compare the `[JIT code]` share and
   `prof.pmu`'s L1I/iTLB misses per 1000 instructions between default and `$OFF`. This is the real test of the
   premise.
3. `kmsmk.sh` with mkjit: `kms.stats=60` through the countdown, and the clock the governor settles at after.
4. **`exact.sh` on Golden Sun: Dark Dawn and HeartGold** (we don't have either here):
   `exact.sh mkjit-on <bin>`, `exact.sh mkjit-off <bin> $OFF`, `exact.sh mkjit-fs <bin> --set jit.flagstub=on`,
   and the base binary. The md5 lists must match. Golden Sun is the timing-sensitive one. HeartGold is also where
   jitcode's original estimate came from (`hgquick.sh` with `EXTRA="$OFF"`, CLK=1104000).

## Exactness run (evidence: `mkjit-evidence/`)

Builds: device build (`build-a64-device.sh`), qemu-dev.
- **MK scene** (mk-scene.sh, fresh save): all 8 pictures (1500-2100) and `audio.raw` match mkbase's reference
  (mkside's s-base). Checked for all on, all off, each cumulative combination, and the branch default
  (`mk-scene-md5.txt`).
- **Pictures 1700/1900/2100/2400 + audio** at 2400 frames: base 00c34c78 against mkjit default and against
  `jit.flagstub=on` (`frames-2400.txt`).
- **Main RAM**: `bananads-difftest --every --from 1600 --frames 2100 --fresh-save --input <scene>` (geomlazy's
  61386d8, applied uncommitted in my build only), each toggle off vs on with the other two on. Result: SAME ×3
  (`mk-difftest.txt`).
- **jit's `exact-suite.sh`**: all three off vs on gives FAILS=0. Each toggle off vs on (the others on) gives
  FAILS=0 for `entry_x26`, `dyncold` and `flagstub` (`ex-*.txt`).
- **Sabotages of 0042 on MK** (`sab-flagstub-mk.txt`, `sab-flagstub.patch.txt`: one build, `MELONDS_JIT_SABFS=n`
  picks the variant):
  - Caught by MK's pictures: ExitCheckF without its AND; both stubs without the AND; both stubs' flags inverted;
    both without the ORR; the blocks calling the plain stubs. A control (other exits skip the CPSR store) is
    caught too.
  - **Not caught** by pictures, audio or main RAM: jitcode's two sabotages (ExitCheckF into the c=0 stub;
    ExitCheckPCF without its AND), **and even ExitCheckPCF's flags inverted or not stored at all**. In this scene,
    the flags at all 7,271 ExitCheckPCF exits are dead.
  - Per the guide's rule (don't turn on an uncaught toggle), **`jit.flagstub` defaults to off**. This is
    BananaDS's setting (`emu.cpp` defaults it to "off"). The patch is unchanged, and `jit.flagstub=on` turns it
    on.
- 0040/0041: jitcode's checks apply (0041's +1-cycle sabotage was caught by the suite; a wrong x26 faults at once).
  I made no new sabotage for them.

## How they combine with session 2's loop patches (0030 `jit.slice_chain`, 0031 `core.halt_skip`)

Local branch **`claude/bananads-perf-mkjit-loop`**, head `2c7d88b`, **not pushed** (git object store shared with
the perf3 worktrees). It is mkbase + 4104d19 (0030) + e644a58 (0031) + c0b6deb/539b14e/7a055f6/2c7d88b (0040-0042
rebased + the default-off commit). How it was made:
- The 004x patch files were rebased with a 3-way merge in a melonDS scratch repo (0001-0020, then 0030-0031, then
  0040-0042). The conflicts are context only:
  - 0040: the Compiler constructor's option lines (0030's `OptSliceChain` block, then 0040's `OptNoPrologue`
    line) and the header's option list (0030's `OptSliceChain`/`SliceChainStub`, then 0040's `OptNoPrologue`).
  - 0041 applied cleanly.
  - 0042: Gen_ExitStubs. 0030's `if (chain) { SliceChainStub = ...; callChain(); RET(); }` block is closed first,
    then 0042's `if (OptFlagStub) { ExitCheckF ... B(ExitCheck[num][1]); }`. Both sit inside the `else` of
    `!OptExitStub`.
- BananaDS text conflicts (SETTINGS.md rows, emu.cpp setting blocks) are a union: both kept.
- Semantics:
  - ExitCheckPCF falls into ExitCheckPC[1] and ExitCheckF branches to ExitCheck[1]. Both now include 0030's chain
    path, which stores RCPSR when c=1. The flags are already in RCPSR by then, as they were when the block flushed
    them itself.
  - 0030's SliceChain9 is a C call. x26 is callee-saved, so 0040's "x26 set only by ARM_Dispatch" still holds on
    the chain path.
- Checked on the combination: the MK scene's 8 pictures and audio match mkbase; main RAM from 1600, jitcode's
  three off vs on: SAME; `exact-suite.sh` all three off vs on (0030/0031 on): FAILS=0.
- mkside (building the integrated `claude/bananads-perf-mkall`) was told to cherry-pick the four -loop commits on
  top of its own 0030/0031. One watch point: 0040 changes NDS.cpp's ARM_Dispatch call (the fast memory base as a
  third argument), near mkside's 0071 in ExecuteJITInline.

## What the next session should know

- On MK, ExitCheckPCF's exits never have live flags, so no check here can see a mistake in that stub. ExitCheckF's
  exits are checked well by MK (the game diverges by frame 1500). To turn `jit.flagstub` on by default, either:
  - a test whose flags live across an exit whose R15 the stub stores (a block ending on a non-branch instruction,
    a flag-reading instruction first in the next block) must catch the PCF sabotages; or
  - 0042 is cut to ExitCheckF only. That is 5,920 of the 13,191 sites, about 1.6% of the bytes, and fully checked
    on MK.
- jitcode's sabotage 1 (ExitCheckF into the c=0 stub, no memory CPSR store) is not caught on MK. When only the
  plain exits skip the store, the game does diverge. So the memory CPSR matters after some exits but not after
  ExitCheckF's here.
- The footprint direction's expected gain on MK is small (+0.2-0.5% by the PMU arithmetic). Wait for the device's
  L1I/iTLB counters before more footprint work. The iTLB rate (4.15/1000) is high for a 4.4 MB JIT region plus the
  binary: huge pages for the JIT's code buffer might do more than bytes.
- Scripts: `mkjit-evidence/mkcensus.sh` (scene + census), `mkdiff.sh` (mkside's MK difftest wrapper),
  `sabchain.sh`, `bltargets.py` (which stub entries the blocks call). The difftest needs geomlazy's 61386d8.

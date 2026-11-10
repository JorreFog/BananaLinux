# mk3jit: the JIT code's cost on Mario Kart DS's race start, and its slow paths (session 3 phase 3, 2026-10-19, 12:05-13:40 UTC)

Branch **`claude/bananads-perf-mk3jit`** on JorreFog/SuperDrastic, from the mk2 checkpoint `3a743f3`. Head **`88008b0`**
(`88008b00439ebc166fed625c22d958fb3efb8e73`), pushed. CI `bananads-device` passed on the head (run 39260854321):
artifact **`bananads-aarch64-88008b00439ebc166fed625c22d958fb3efb8e73`**. **Untested on device.**

melonDS patches **0085-0087** (0088-0089 unused). No `SlowPathFlags`/`MELONDS_MEMFAST` bit used. Shared files with
`mk3geom`: `emu.cpp` (my blocks sit after `jit.flagstub`'s and after `SetRegionPair`), `docs/SETTINGS.md` (rows
after `jit.flagstub` and `jit.region_pair`), `docs/PERF.md` (my section sits just before "## BananaJIT"). New test
ROM: `bananads/tests/cp15nop/` (built by its `build.py`, clang, like jitloops).

| # | Commit | Patch | Toggle (default) | What | qemu (MK 1650-2150, a frame) | Expected on device |
|---|---|---|---|---|---|---|
| 1 | 5919646 | 0085 | `jit.hugepages` (`on`; `MELONDS_JIT_HUGEPAGE=0` off) | the JIT's 32 MB code memory aligned to 2 MB and `madvise(MADV_HUGEPAGE)`'d before its first touch (session 1's jit 0021, never merged into BananaDS, ported unchanged) | none (qemu has no TLB) | **+0.3..1.5%**, the least certain |
| 2 | c1019bc | 0086 | `jit.cp15nop` (`on`; `MELONDS_JIT_NOCP15NOP=1` off) | an ARM9 `MCR p15` to a c7 cache/write-buffer operation (which melonDS's CP15 ignores) compiled to its cycles instead of the interpreter's call; user mode keeps the call (far region) | **-76k** (`A_MCR` -32k, `CP15Write` -24k, `AddCycles_CI` -9k, JIT code -11k) | **+0.3..0.5%** |
| 3 | 88008b0 | 0087 | `jit.region_trio` (`on`; off = 0071's pair) | two code regions kept for the swap back, the ARM9's BIOS and the ARM7's shared WRAM among them, and the wake-up slice (`Execute<JIT>`) swaps too | **-34k** (setups -68k, swaps +34k); setups ~640 -> ~20 a frame | **+0.2..0.4%** |

Together: the emulator thread **12.590M -> 12.458M instructions a frame without Gengis's bins (-1.05%; -110k of
it deterministic, the rest Gengis/2D-thread noise)**, and **+0.8..2.4% expected on the device** (0085 is most of the
spread). All three default on; all three are exact (below).

`OFF="--set jit.hugepages=off --set jit.cp15nop=off --set jit.region_trio=off"` gives exactly the mk2 code paths
(and 3a743f3's JIT layout).

## Where the JIT code's time goes (the brief's first job)

MK scene, frames 1650-2150, qemu per-TB counts mapped to `MELONDS_JIT_PERFMAP=1`'s blocks and disassembled from its
code dump (`mk3jit-evidence/jitblocks.py`, output `jit-blocks-mk-3a743f3.txt`):
- **The JIT code is 1.54M of the thread's 13.27M instructions a frame (11.6%) but 29.1% of the device's samples (mkbase's profile):**
  its IPC on the A55 is ~0.2 against the thread's 0.49. Its cost is mostly stalls (guest loads, L1I/iTLB, branches),
  not instructions; qemu can't rank those. That is why 0085 (TLB) is here even though qemu can't see it.
- **The shared stubs are a third of the JIT's instructions:** ExitCheck 163k (ARM9) + 37k (ARM7) (~10.3k exits a
  frame), the ARM9's JumpTo 140k (~7,000 register jumps, 20 instructions each: the region timing table lookup),
  DynExit 59k + 10k, the patched memory calls' save/restore (`stub_eb00`) 60k.
- The hottest guest block is a word-copy loop at 0x0214d048 (71k, ~1,900 iterations a frame at ~37 host
  instructions for 4 guest ones: registers reloaded every iteration, the flags put in the CPSR, an exit check;
  LOOPREGS, off since HeartGold's black screen, is what would fix it), then a long tail: the top 50 blocks are 55%.
- Dynamic mix: ALU 30%, constants 13% (the exit check's timestamp address is three MOV/MOVKs at ~18k checks a
  frame), CPU-state loads/stores 15%, flags 7%, cycles 5%, guest memory 4%.
- Around the JIT: the JIT compiling the race's new code (~4.5 blocks a frame, ~214k instructions a frame, 1.6%:
  `RegisterCache::Prepare` 46k, `IsImmLogical` 43k, `ARMJIT::CompileBlock` 36k, the emitter's `WritesNZCV` hook 20k
  and `Write32` 16k), the interpreted MCRs (~1,000 a frame: 0086), the region setups (~640 a frame: 0087), the GX
  port STRs through the patched stubs (~1,830 a frame, below).

## Expected gains, and why

1. **0085 `jit.hugepages`.** The device profile has 61k L1 iTLB misses a frame (4.15 per 1000 instructions). MK
   compiles 4.4 MB of blocks (~1,100 4 KB pages) plus the far region, and the A55's 1024-entry L2 TLB is shared with
   the data side (main RAM's fastmem alone is 1,024 pages). With 2 MB pages the JIT's code takes a few entries. If
   half the iTLB misses are the JIT's at ~5 cycles (L2 TLB hit) to ~25 (a walk): 0.15-0.75M of the frame's 29.95M
   cycles; more if the data side's walks drop. Session 1's model said -0.4..-0.7% on smaller code. The log line
   `[jit] code memory at <addr>, 32768 kB, N kB on huge pages` says whether the kernel (THP `madvise`) gave them:
   N must be 32768. Under qemu it reads 0 (expected).
2. **0086 `jit.cp15nop`.** ~75 instructions, two calls (one virtual) and a register flush/reload per MCR, ~1,000 a
   frame (DC_FlushRange's loop: c7,c10,4 + c7,c14,1 per line), at the thread's ~2 cycles an instruction plus the
   calls: 0.1-0.15M cycles a frame.
3. **0087 `jit.region_trio`.** On mkbase's profile `GetMirrorLocation` + `SetupExecutableRegion` + `ClassifyAddress9`
   were 1.07%; 0071 took ~40% of their work; 0087 takes nearly all the rest (incl. `GetMirrorLocation`'s indirect
   switch) but its swap costs ~35 instructions against the pair's ~24 at ~1,650 ARM9 region changes a frame.

## Device commands (most useful first)

Binary: the CI artifact above. Base: mk2's artifact `bananads-aarch64-3a743f32a9bd8726645980a61768ddf9bf2efa4e`.
1. `mk.sh` headless at 1992 MHz, frames 1650-2150 and 1700-2700: mk2 vs mk3jit (default), and mk3jit with
   `EXTRA="$OFF"` (same binary, old paths). Then one toggle off at a time (`--set jit.hugepages=off`,
   `--set jit.cp15nop=off`, `--set jit.region_trio=off`). Report the slowest 100-frame window. Check the log line
   `[jit] code memory at ..., 32768 kB, 32768 kB on huge pages`.
2. The same with `--set prof.hz=1000`. Compare: the `[JIT code]` share and `prof.pmu`'s iTLB (and L1I) misses per
   1000 instructions, default vs `--set jit.hugepages=off` (the test of 0085's premise); `A_MCR`/`CP15Write` (gone
   with 0086); `SetupExecutableRegion`, `GetMirrorLocation`, `ClassifyAddress9` (mostly gone with 0087) against
   `SwapCodeRegion3`.
3. `kmsmk.sh` with mk3jit: `kms.stats=60` through the countdown, and the clock the governor settles at.
4. **`exact.sh` on Golden Sun: Dark Dawn and HeartGold:** `exact.sh mk3jit-on <bin>`, `exact.sh mk3jit-off <bin> $OFF`,
   and the mk2 binary. The md5 lists must match. Both games use NitroSDK's cache MCRs (0086) and IRQs through the
   ARM9 BIOS (0087); HeartGold's ARM7 may run from shared WRAM too.

## Exactness run (all under qemu, device build; evidence in `mk3jit-evidence/`)

- **MK scene** (`mk-scene.sh`, 8 pictures + audio): identical to the base hashes with the head's defaults, with each
  new toggle off (`jit.cp15nop=off`, `jit.region_trio=off`), and with all three off (`mk-scene-md5.txt`,
  `mk-scene-head.txt`). **Frames 1700/1900/2100/2400 + audio at 2400**: mk2 (3a743f3) vs the head, identical
  (`frames-2400.txt`).
- **Main RAM + both CPUs' registers + the geometry engine's timing incl. the ARM9's timestamp, every frame
  1500-2100** (`bananads-difftest --every --regs --timing --fresh-save --input`): `jit.cp15nop` off vs on SAME,
  `jit.region_trio` off vs on SAME (`mk-difftest-*.txt`).
- **Full savestates** at 1850 and 2000 (`gl/sst.sh`): mk2, the 0086 build (on, and with 0085/0086 off) and the head
  all give the checkpoint's `a57737e0...` / `f476c39e...` (`savestates.txt`).
- **jit's `exact-suite.sh`** (17 ROMs' pictures, 12 difftests incl. irqmask, ipcrom, dmasnd, `bananads-tests`):
  `jit.cp15nop`+`jit.hugepages` off vs on FAILS=0; `jit.region_trio` off vs on FAILS=0.
- **`tests/cp15nop`** (new): DC_FlushRange's loop with every register live, conditional MCRs, timer reads, then
  user-mode MCRs (R13/R14 known constants: the old call; not known: 0086's far region) 1,000 times. Off vs on, 300
  frames, main RAM + registers + timing every frame: SAME. The user-mode results show the JIT's old behaviour kept
  (the block goes on in UND mode after A_MCR's exception, as melonDS always did).
- A clean build of the head (pristine melonDS + patches by CMake) is byte-identical to the binary the checks ran on.
- **Sabotages** (one build each, an env variable picks the variant):
  - 0086, the fast path's cycles one short (`CI(1)`): MK's audio and every picture from 1740 differ; tests/cp15nop's
    timer differs. Caught.
  - 0086, the far region not taking the cycles back: tests/cp15nop's timer differs (only after the ROM was made to
    run the user-mode MCR 1,000 times: one is absorbed by the slice). Caught by the ROM only: MK never runs an MCR
    in user mode.
  - 0087, a swapped-in BIOS/shared WRAM/WRAM region's start 4 bytes off: MK hangs at boot. Caught.
  - **Not caught by anything here:** 0087's shared WRAM Mem/Mask check (MK doesn't remap its shared WRAM in the
    scene; no test ROM runs ARM7 code from shared WRAM across a WRAMCNT change). Golden Sun/HeartGold `exact.sh` is
    the check. 0085 has no sabotage: it changes no emulated state (only the code memory's address).

## Dead ends and things not done (also in DEAD-ENDS.md)

- **The exit check's timestamp address from x29** (to drop three MOV/MOVKs from ~18k checks a frame): `NDS::ARM9Timestamp`
  is ~79 MB before `NDS::ARM9` (the NDS object holds the JIT's tables), out of reach of one ADD/SUB; a load of the
  NDS pointer first is a dependent load. Not done.
- **A GX-only entry for the JIT's STRs to the GX FIFO port** (the brief's candidate): after 0064, `SlowWrite9`'s GX
  case is already first and direct; what is left per STR is the patched stub's save/restore of x8-x15 and NZCV (16
  instructions), ~15 in `SlowWrite9` and ~20 in `WriteToGXFIFO`'s quick parameter path. A special stub would save
  ~15-25 of them, ~30-50k a frame (2,076 STRs). Not done: small for the risk in the fault-patching code.
- The patched memory stubs could skip saving x8-x15 where no guest register lives there (per site, from
  `RegCache.NativeRegsUsed` at compile time): ~8 instructions a call, ~30k a frame. Not done.
- 0087's swap is ~35 instructions (it loads three region pointers from the JIT object far from `NDS`); caching them
  in the CPU would take ~5-8 off each of ~1,650 swaps a frame.

## What the next session should know

- **The device decides 0085.** If `prof.pmu`'s iTLB misses per 1000 instructions don't drop with `jit.hugepages=on`
  (and the log line says 32768 kB on huge pages), TLB pressure isn't the JIT's problem; if they drop but fps doesn't
  move, the JIT's stalls are elsewhere (guest data loads, branches).
- The JIT's 29% is ~1.5M instructions a frame at IPC ~0.2: further instruction cuts in JIT code move little unless
  they remove loads/branches. What qemu shows as big and cuttable is outside the blocks: the stubs (ExitCheck,
  JumpTo, DynExit ~0.4M a frame), the JIT compiling new code (~0.2M a frame during the race start; `IsImmLogical` alone is 43k for
  ~4.5 blocks a frame, which looks like far more calls than constants: who calls it that often is unchecked; the `WritesNZCV` hook runs per emitted
  instruction), the patched memory stubs.
- The word-copy loop at 0x0214d048 and similar self-loops are the case for LOOPREGS (patch 0007's, off since
  HeartGold's black screen at its first overworld frame). Finding that bug would help MK's hottest block.
- Tools: `mk3jit-evidence/jitblocks.py` (per-block and per-instruction-class counts from a `qc.sh` run with the code
  dump kept), `dumpblk.py` (a block's disassembly with per-TB counts), `mcrsites.py`/`callsites.py` (which guest
  instructions/blocks call a host function, weighted), `symdiff.py` (per-symbol diff, LTO suffixes normalised),
  `region-setups-mk.txt` (the instrumented setup counts).

## Follow-up (14:00 UTC): the JIT compiler's own cost, and the shared stubs

New head **`f1c467d`** (`f1c467dd8a43f1402d25a38a5c76a6bea34af73d`), pushed, one commit after 88008b0. CI
`bananads-device` passed (run 39264549553): artifact **`bananads-aarch64-f1c467dd8a43f1402d25a38a5c76a6bea34af73d`**.
Untested on device. `OFF` above gains `--set jit.fastclz=off`.

| # | Commit | Patch | Toggle (default) | What | qemu (MK 1650-2150, a frame) | Expected on device |
|---|---|---|---|---|---|---|
| 4 | f1c467d | 0088 | `jit.fastclz` (`on`; `MELONDS_JIT_SOFTCLZ=1` off) | the AArch64 emitter's `CountLeadingZeros` (inside `IsImmLogical`) by `__builtin_clzll` instead of Dolphin's bit-by-bit loop | **-38k** (`IsImmLogical` -40k, `CountLeadingZeros` +2k) | **+0.1..0.3%**, only while new code compiles |

- **Who calls `IsImmLogical` so often:** nobody. An instrumented build counted ~40 calls a frame in the window
  (nearly all from `ANDI2R`). The 43k was the cost of each call: four `CountLeadingZeros` as a loop of up to 64
  rounds, ~1,100 instructions a call. The count is the same with CLZ, so every encoding is too.
- **Exactness (compile-time only):** the MK scene with `MELONDS_JIT_PERFMAP=1` under `setarch -R` (with ASLR the
  dumps differ in heap address constants between any two runs) gives a code dump of every block and stub (5.7 MB)
  and a perf map **byte-identical** between `jit.fastclz` on and off; pictures and audio = base both ways;
  `exact-suite.sh` off vs on FAILS=0. **Sabotage:** CLZ one too many for one value: the code dump differs from the
  same build's control and bananads dies on an illegal instruction (exit 132). Caught. Evidence:
  `fastclz-codedump.txt`, `exact-suite-fastclz.txt`.
- **The `WritesNZCV` hook** (19.5k a frame): `Write32` calls it twice per emitted instruction (the lazy-flags check,
  then the epoch), ~1,300 instructions emitted a frame. Calling it once would save ~10k a frame. Not done: too small
  for a commit.
- **The shared stubs** (ExitCheck ~10.3k calls a frame, JumpTo ~7k, DynExit ~2.4k): nothing qemu can rank as a
  worthwhile cut that removes loads or branches.
  - JumpTo's 20 instructions are the region table lookup, the ITCM check, the mode switch and the R15/cycles
    stores, with no slack.
  - DynExit could take its target from w4 instead of reloading R15 (one load and two ALU instructions, ~2.4k a frame:
    ~7k instructions).
  - The exit check's timestamp address can't come from x29 (DEAD-ENDS).
  - **For the device (not qemu):** ExitCheckPC (~8.1k calls a frame) loads the exit's R15 from the call site's
    literal (`ldr w0, [x30, #4]`): a data load from code memory, which the A55 serves from L1D only if the line is
    there, otherwise L2/L3. If the device shows the JIT code's L1D misses high, storing R15 in the block again (what
    `MELONDS_JIT_NOEXITPC=1` does, at ~8 bytes per exit site; it also turns off the self-loop exits, which depend
    on it) is the A/B to run. There's no setting for it: set the environment variable.
- The compiler's remaining cost (~0.17M a frame: `RegisterCache::Prepare` 46k, `ARMJIT::CompileBlock` 36k, the emitter
  ~35k) is real work per instruction compiled.

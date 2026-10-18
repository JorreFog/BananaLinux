# jitcode: the JIT's code footprint (second speed session, 2026-10-18, 19:40-20:38 UTC)

Branch **`claude/bananads-perf-jitcode`**, head **`96b339f`** (from `00c34c78e8de`), pushed. Four commits (three changes and a docs note), one
melonDS patch and one toggle each (`bananads/patches/melonds/0040-0042`, all default `on`, `off` = the old code).
Everything below is **untested on device**: the numbers are static code sizes, not speed.

## Why code size

The JIT's generated code is 32% of the emulator thread in HeartGold (device profile, 2026-10-12). 0007 cut ~40%
of its instructions on cpubench and nothing showed on the device; on tests/bigcode (game-sized footprint) the
first session's rgds-sim run put the JIT code at ~0.25 IPC: fetch-bound. So the target was bytes per block.

## Census (tests/bigcode-thumb, first 60 frames, 3863 blocks, device build; `census.py` on the perf map's blocks)

Where the base's block bytes go (1,170,716 bytes, 303 bytes/block, 292,679 instructions): ALU 36%, guest
register stores 11%, constants 11%, flags/CPSR 10%, register loads 6%, B 5%, fast memory 5%, BL 4%, cycles 4%.
Repeated sequences that cost bytes without doing guest work, per block:

| Sequence | Sites | Bytes | Share | Status |
|---|---|---|---|---|
| entry prologue `mov/movk/movk x26` | 3863 | 46.4 KB | 4.0% | done: 0040 |
| flags flushed before the exit's `bl ExitCheck(PC)` (mrs/and/orr) | 3016 | 36.2 KB | 3.1% | done: 0042 |
| `mov w0,#v; str w0,[x29,#272]` = ARM9 RegionCodeCycles store at every constant jump | 4276 | 34.2 KB | 2.9% | idea 1 below |
| DYNCACHE return exit's miss way (cmp/b.eq/b/bl/b) | 1739 | 27.8 KB | 2.4% | done: 0041 |
| `adrp/add/str` R15 store before a block's non-branch last instruction | 3110 | 37.3 KB | 3.2% | idea 2 below |
| Thumb NZ flags `cset/bfi/cset/bfi` (-> `mrs/lsr/bfi`) | 1954 | 7.8 KB of which 1 insn saved | 0.7% | idea 3 |

| Build | Block bytes | Bytes/block | Instructions in blocks |
|---|---|---|---|
| base (0001-0020) | 1,170,716 | 303 | 292,679 |
| + 0040 | 1,124,360 (-4.0%) | 291 | 281,090 |
| + 0041 | 1,096,536 (-6.3%) | 284 | 274,134 |
| + 0042 | 1,060,344 (-9.4%) | 274 | 265,086 |

(0041 moves 20 bytes per site to the far region; 0042 adds 2x28 bytes of shared stubs.)

## The changes, ranked by expected device gain

Reasoning for all three: the first session's rgds-sim run of 0024 (= 0040) on bigcode gave ~0.4% less main-thread
time per 1% less JIT code, with JIT code 60% of that thread. If HeartGold's JIT code (32% of the emulator thread)
is as fetch-bound as bigcode's, 1% fewer block bytes ~ 0.2% of the emulator thread. If HG's hot code fits the
A55's L1I, all of this is ~0. That is a model-derived estimate (DEAD-ENDS: models were wrong before); only the
device can say.

1. **0040 `jit.entry_x26`** (cc73062): the first session's 0024 ported (renumbered; ported because it is the
   cheapest footprint win and already exact). ARM_Dispatch takes the fast memory base as a 3rd argument and sets
   x26; blocks lose the 12-byte prologue; a dispatch runs 3 instructions fewer. -4.0% bytes -> **~+0.8% fps**.
   `loop` was told (its 0030 keeps x26 valid; ARM_Dispatch's one `mov x26, x2` is the only Linkage.S change).
2. **0042 `jit.flagstub`** (863d297): exits with pending lazy flags call new stub entries ExitCheckPCF /
   ExitCheckF that do the mrs/and/orr once, instead of 12 bytes in every such block. Same instructions run (+1 B
   on ExitCheckF). -3.1% -> **~+0.6%**. Its exactness check is weak (sabotages not caught, below).
3. **0041 `jit.dyncold`** (bf60358): a register return's cached exit keeps its 20-byte miss way in the far region
   (after the learn path) behind one B. -2.4% -> **~+0.5%**. Risk: a polymorphic return (misses often) runs one
   B more and fetches a far line; DynExitA's lookup after it is ~20 instructions anyway.

All three together: -9.4% block bytes -> ~+2% fps if fetch-bound, else ~0.

## Device A/B (each against a plain run of the same binary; most promising first)

```
CLK=1104000 EXTRA="--set jit.entry_x26=off --set jit.dyncold=off --set jit.flagstub=off" hgquick.sh <bin>   # all three
CLK=1104000 EXTRA="--set jit.entry_x26=off" hgquick.sh <bin>
CLK=1104000 EXTRA="--set jit.flagstub=off" hgquick.sh <bin>
CLK=1104000 EXTRA="--set jit.dyncold=off" hgquick.sh <bin>
threads.sh <bin> --set jit.entry_x26=off --set jit.dyncold=off --set jit.flagstub=off     # emulator thread %, vs threads.sh <bin>
exact.sh jitcode-on <bin>; exact.sh jitcode-off <bin> --set jit.entry_x26=off --set jit.dyncold=off --set jit.flagstub=off
```
If the device has `perf` by then: `perf stat -e L1-icache-load-misses,iTLB-load-misses` all-on vs all-off on the
emulator thread is the mechanism check (expect fewer L1I misses; if not, the premise is wrong for HG).

## Exactness checks run (device build `build-a64-device.sh`, qemu-dev)

- `evidence/jit/exact-suite.sh` (pictures every 50 frames to 300 on 17 ROMs incl. jitloops x3, irqmask x3, iorom
  L5, ipcrom, dmasnd audio; `bananads-difftest --every --frames 300` main RAM on 9 ROMs; `bananads-tests`),
  toggle off vs on: `jit.dyncold` FAILS=0, `jit.flagstub` FAILS=0, `jit.entry_x26` FAILS=0 (bB, all three
  patches in; jitcode-evidence/ex-*.txt).
- A clean build dir applies 0001-0042 to pristine melonDS and builds (bB).
- Sabotage: 0040 none this session (a wrong x26 faults at once; the first session's 0024 checks apply).
  0041 with one cycle added on the far miss way: **FAILS=12** (cpubench, jitloops, irqmask: caught).
  **0042: neither of two sabotages was caught**: ExitCheckF's B to the c=0 ExitCheck (no CPSR store; quick suite
  FAILS=0) and ExitCheckPCF without its AND (old flags OR'd with new; full suite FAILS=0,
  ex-sab2-flagstub.txt). The census shows the blocks do call the new entries, so the test ROMs' flags at those
  exits are dead (rewritten before read) and the frame-end CPSR compare doesn't see them. 0042 is exact by
  construction (the same three instructions, moved from before the BL to the BL's first instructions, with the CPSR
  store the block already chose), but **its check is weak by the brief's rule**: treat it as the least sure of the
  three, run the device's `exact.sh` with `--set jit.flagstub=off` against on before trusting it, or leave it off.
  A ROM whose flags live across a block exit (a conditional after a call-free exit into a linked block) would be
  the missing test.
- Not done: bigcode pictures (not in the suite), `tests/jit_encodings.sh` (it tests BananaJIT's encoder; no
  emitter encoding changed here, the disassembly of the new code was read with objdump).

## Risks

- 0040: anything entering a block other than ARM_Dispatch, a linked slot, DynExit, a self loop or the fault
  handler's restore would run without x26 (read: there is nothing else; same as the first session's 0024).
- 0041: the far region (4 MB) fills ~20 bytes per return site faster; a game near the limit resets the JIT cache a
  little sooner. A B from the block to the far region relies on the emitter's RW-view distance (as the slots do).
- 0042: the stub's CPSR store is now always made (it was already, CPSRDirty is required). Weak sabotage coverage
  for ExitCheckF, see above.
- **Composition with `loop`'s 0030-0032**: 0040-0042 are made against 0001-0020. On top of loop's patches they
  conflict only in context (the Compiler constructor's option lines, the header's option list, Gen_ExitStubs
  around loop's chain stub); a union resolution looked mechanical but was not finished or tested. Whoever merges
  both branches must rebase 004x onto 003x and rerun the suite.

## Next, if the device shows the footprint matters

1. ARM9 RegionCodeCycles store at every constant jump (2.9% of bytes, 2 instructions each): read only by
   `CP15::CodeRead32` (the interpreter's fetch, FillPipeline) and set again by every interpreter `JumpTo`; if no
   reader sees it between a JIT jump and the next `JumpTo`/IRQ, it can go (or move into the exit stubs). Needs a
   proof that savestates and FillPipeline don't read a stale value.
2. R15 store before a non-branch last instruction (3.2%): defer it to ExitCheckPC's literal (4 bytes) when the last
   instruction can't fault or call (pure ALU).
3. Thumb NZ flags: `mrs x0,nzcv; lsr w0,w0,#30; bfi w27,w0,#30,#2` instead of cset/bfi x2 (0.7%).
4. Hot/cold block placement (compile order = first-run order, so hot loops sit among init code) and a 16-byte
   alignment check of hot block entries: only with a device profile of HG's L1I misses.

If the device shows nothing for -9.4% bytes, stop footprint work: HG's hot JIT code then fits the caches and the
JIT's 32% is ALU work, where only fewer guest-visible operations (not bytes) help.

Evidence: `perf/bananads/session2/jitcode-evidence/` (suite outputs, census outputs, census.sh, pat.py). Build
dirs: /home/user/perf2/jitcode/bB (all three), bS/bS2 (sabotages).

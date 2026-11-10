# Session 2, agent `loop`: the slice loop, the JIT's exits and halts (emulator thread)

Branch **`claude/bananads-perf-loop`** on JorreFog/SuperDrastic, from `00c34c78e8de`. Head **`275e1a0`** (pushed; the last commit is docs only).
Two commits, two melonDS patches (`bananads/patches/melonds/0030`, `0031`), each with a setting whose `off` is the
old code. Everything is **untested on the device**; qemu instruction counts below only show that the path got
cheaper.

## The commits, ranked by expected HeartGold gain

| # | Commit | Toggle (default) | What |
|---|---|---|---|
| 1 | `f28a4d6` melonDS 0031 | `core.halt_skip` (`on`) | both CPUs halted, nothing to wake them: `RunSlicesJIT` takes the times straight to the last 64-cycle slice end before the next event/timer instead of slice by slice |
| 2 | `0c19098` melonDS 0030 | `jit.slice_chain` (`on`; env `MELONDS_JIT_SLICECHAIN=0`; off with `jit.codegen=old`) | an ARM9 exit stub that finds its slice over calls `NDS::SliceChain9`; when the loop's iteration would only keep time (ARM7 halted or ahead, nothing due, GX idle, no DMA) it does that bookkeeping and the chain goes on, without ARM_Ret / the loop / ARM_Dispatch |

### Expected device gain, with reasoning

1. **core.halt_skip: 0 to +5% fps, most likely +1..3%.** The device profile has the scheduler/RunFrame at 10% of the
   emulator thread. On membench (the only test ROM whose ARM9 and ARM7 both halt between frames, as a Nitro game's
   do: CP15 wait-for-interrupt + HALTCNT), 43% of the slice loop's instructions were halted slices: the loop went
   920k -> 523k instructions/frame, the main thread -6.6% (qemu-count). If HeartGold's both-halted share is like
   membench's, half of that 10% goes; if the game's ARM9 rarely waits (it is heavy at 72 fps), less. The device can
   say before any fps run: `MELONDS_HALTSKIP_STATS=1` prints `[core] halt_skip: N skips in F frames`; each skip
   replaces ~16 slices (~45 instructions each). Cost when it never fires: ~1 instruction a slice (L1: +0.06%).
2. **jit.slice_chain: -1..+3%, uncertain sign.** It removes, per ARM9 slice end with the ARM7 waiting, ARM_Ret's and
   ARM_Dispatch's 24 register loads/stores, a mispredicted return, the indirect branch into the block and the
   loop's lookup, and adds a call of ~60 instructions (loads/compares of far NDS fields). qemu-count: cpubench Thumb
   (ARM7 halted) main thread 4,896k -> 4,696k (-4.1%); membench -0.2% (its ARM9 halts, halt_skip's case);
   **cpubench with a busy ARM7: 5,381k -> 5,463k (+1.5%)**: every call bails (the ARM7 is behind and running). In
   HeartGold the ARM7 (sound/IPC driver) is mostly halted, so most slice ends should chain; the A55 should gain more
   than the instruction count says (two mispredicts and 24 memory ops gone per chained slice), but that is the
   assumption the device has to test. A version that checks the ARM7 first (bail in ~15 instructions) is in
   `loop-evidence/unpushed-melonds/0001` but was not re-run through the full suite in time, so it is not on the branch.

## Device A/B commands (most promising first; all one binary)

```sh
# how often both CPUs wait in HeartGold (run with hgquick's settings, read the line at exit)
MELONDS_HALTSKIP_STATS=1 <bin> --frontend headless <hgquick's --set ...> hg.nds 2>&1 | grep halt_skip
CLK=1104000 EXTRA="--set core.halt_skip=off" hgquick.sh <bin>          # 0031 (against plain)
CLK=1104000 EXTRA="--set jit.slice_chain=off" hgquick.sh <bin>         # 0030 (against plain)
CLK=1104000 EXTRA="--set core.halt_skip=off --set jit.slice_chain=off" hgquick.sh <bin>   # both
threads.sh <bin>; threads.sh <bin> --set core.halt_skip=off --set jit.slice_chain=off   # emulator thread's busy %
exact.sh loop-on <bin>; exact.sh loop-off <bin> --set core.halt_skip=off --set jit.slice_chain=off   # md5 lists equal
```
With `tools`' profiler (`hgprofile.sh`, needs its commits f62a109+c27b7d9+c901542 in the tree) the "scheduler" and
"JIT dispatch and exits" areas should both shrink with each toggle on.

## Exactness checks run (device build, Bootlin GCC 14.3 -O2, under qemu)

- `evidence/jit/exact-suite.sh` (pictures every 50 of 300 frames on 17 ROMs incl. dmasnd's audio dump, difftest
  `--every --frames 300` main RAM on 9 ROMs, `bananads-tests`), toggle off vs on, on a **clean build of the pushed
  branch** (CMake applied 0001-0020 + 0030 + 0031): `jit.slice_chain` FAILS=0, `core.halt_skip` FAILS=0
  (`loop-evidence/exact-suite-*.txt`).
- slice_chain also: pictures + audio dump of sndbench, ds2dtest, geomtest, bigcode, L1; `core.slice=auto` on
  ipcrom, cpubench busy ARM7, dmasnd; `render.scale=2` on cpubench and L5; difftest on ipcrom. All identical.
- Sabotage: slice_chain ignoring the ARM7's pending IRQ: caught by ipcrom's difftest (main RAM differs). halt_skip
  ignoring pending IRQs: caught by jitloops' difftest (differs from frame 5). (Three other halt_skip sabotages were
  not caught, by design: once both CPUs are halted the slice grid can't be observed, so only the entry conditions
  matter; the IRQ one is the condition that is exercised. The GX-idle and timer conditions are not exercised by any
  test ROM while both CPUs halt.)
- Which ROMs exercise halt_skip: membench (~1.3k skips a frame), jitloops (16.7k in 200 frames); cpubench, the
  stress ROMs, sndbench, ds2dtest, ipcrom, geomtest: 0 (their ARM9 never halts).

## Risks

- slice_chain: the stub calls C from JIT code at a block exit (x4/x30 saved, everything else callee-saved; guest
  registers are all in memory there). A self loop in LOOPREGS mode (off by default) doesn't chain. With a busy ARM7
  it is pure overhead (+1.5% instructions on cpubench busy ARM7). If a future patch keeps state in caller-saved
  registers across an exit, the call clobbers it.
- halt_skip: the GX-idle and timer-due conditions are unexercised by the test ROMs while both CPUs halt (read
  against the loop line by line; a game that halts with GX commands queued would take the old path, by the
  condition). DSi paths untested.
- Neither changes a slice end that anything can observe; `core.slice` stays 64.

## Not on the branch (unchecked by the 20:38 cut; patches in `loop-evidence/unpushed-melonds/`)

- A port of the first session's 0023 `jit.irqchain` onto 0030's stubs (0003 there). My first merge **hung**
  cpubench (a SliceChain9 bail fell into the masked-IRQ check with W0 = 0 and looped); the fixed version passed a
  quick check (cpubench + irqmask pictures) only. Needs the full suite before use. Its HG value depends on how often
  an IRQ is pending under CPSR.I (unknown; the first session's EXITSTATS would count it).
- slice_chain with the ARM7 test first (cheaper bail, for busy-ARM7 stretches).

## Next, if the device confirms

1. If halt_skip shows: the same for "ARM9 halted, ARM7 busy" (the ARM7's exit stubs chaining its slices) and for
   halted CPUs while the geometry engine drains the GX FIFO (needs GPU3D::Run split exactly at IRQs).
2. If slice_chain shows: the cheaper bail, then the irqchain port (full suite first), then trimming SliceChain9's
   far-field loads (pin the NDS fields it reads into one cache line).
3. The SPU was not touched: `core.fast_mix` (0013) already runs only due channels; `core.fast_spu` is dead code with
   the defaults (tools' audit).

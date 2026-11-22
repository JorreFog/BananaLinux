# jit: findings (melonDS's AArch64 JIT and its runtime; then the 2D output stage)

Agent `jit`, 2026-10-18. Branch `perf/jit` in `/home/user/perf/jit/sd` (base `00c34c78e8de`); patches in
`patches/jit/` (`git format-patch 00c34c78e8de..perf/jit`). melonDS patches 0021-0024 in
`bananads/patches/melonds/`. All numbers: rgds-sim (uncalibrated A55/RK3568 model, deterministic after sim's 16:10
ASLR fix; v1 cost model in Part 1, v2 = + write streaming and v3 = + timed PRFM in Part 2), device-faithful build (`/home/user/perf/tools/build-a64-device.sh`: Bootlin GCC 14.3, -O2), main
thread ms/frame at 1104 MHz, A/B with one binary and the toggle. Exactness suite: `evidence/jit/exact-suite.sh`
(pictures every 50 frames to 300 with `rtc.fixed` on 17 ROMs incl. tests/irqmask, the dmasnd audio dump,
`bananads-difftest --every --frames 300` main RAM on 9 ROMs, `bananads-tests`), toggle off vs on.

## Part 1: the JIT (stopped at 16:54 by the coordinator's plan change)

### What changed (perf/jit commits; every toggle defaults to on; every change exact)

| Commit | melonDS patch | Toggle | What |
|---|---|---|---|
| b0218cd | 0021 | `jit.hugepages` (`MELONDS_JIT_HUGEPAGE=0` off) | The JIT's 32 MB code slice aligned to 2 MB and `madvise(MADV_HUGEPAGE)`'d before its first touch (Reset fills all of it anyway, so no RSS cost). BananaLinux's kernel is THP=madvise. The JIT's first reset logs `[jit] code memory at <addr>, 32768 kB, N kB on huge pages` (from /proc/self/smaps). |
| 7315d05, b178c99 | 0022 | `MELONDS_JIT_EXITSTATS=n` | Prints at exit, per frame from frame n on, why JIT block chains came back to the slice loop: compiles, region setups, IRQs taken, IRQs masked by CPSR.I, halts/idle loops, slice ends with a masked IRQ pending. Counted on the loop's cold paths only. |
| 7315d05, b178c99 | 0023 | `jit.irqchain` (`MELONDS_JIT_NOIRQCHAIN=1` off; `jit.codegen=old` off) | melonDS sets ARM::IRQ whenever IME/IE/IF say so, whatever CPSR.I is; with an IRQ pending in a masked stretch every block exit went back to the loop, which only calls TriggerIRQ (returns at once) and dispatches the next block. The exit stubs (ExitCheck, ExitCheckPC, DynExit, DynExitA) and a self loop's inline check now go on when the only stop reason is an IRQ that CPSR.I masks. |
| 3450884 | 0024 | `jit.entry_x26` (`MELONDS_JIT_NOENTRYX26=1` off; `jit.codegen=old` off) | ARM_Dispatch takes the CPU's fast memory base and sets x26, so blocks lose 0007's FASTENTRY prologue (12 bytes per block, 3 instructions per dispatch). |
| b178c99 | - | - | tests/irqmask (CPSR.I-masked stretches with IRQs pending: handlers, nesting, IME/IE changes, SVC mode, CP15 halt; Thumb, ARM, busy ARM7) and tests/bigcode (a game-sized code footprint: 500 generated functions, ~1.5 MB of JIT code). Built copies: `/home/user/perf/jit/bin/`. |

### Measured (rgds-sim v1, device build)

| Change | Workload | ms/frame @1104 A -> B | Evidence |
|---|---|---|---|
| jit.hugepages | cpubench-thumb | 5.719 -> 5.694 (-0.44%) | evidence/jit/thp-sim-dev-cbthumb.txt |
| jit.hugepages | tests/bigcode-thumb (frames 20-50) | 10.258 -> 10.184 (-0.72%; iTLB L2 hits -61%, dTLB L2 hits -42%) | evidence/jit/thp-sim-dev-bigcode.txt |
| jit.irqchain | tests/irqmask-thumb-arm7 | 9.487 -> 7.881 (-16.9%; masked-IRQ returns 25.1k+1.7k/frame -> 0) | evidence/jit/irqchain-sim-irqmask.txt, irqmask-exitstats.txt |
| jit.irqchain | test ROMs other than irqmask | no change (they have ~0 masked IRQs: jitloops 0.7/frame) | evidence/jit/exitstats-testroms.txt |
| jit.entry_x26 | tests/bigcode-thumb | 10.158 -> 9.987 (-1.68%; L1I misses to DRAM -4.6%) | evidence/jit/x26-sim-bigcode.txt |
| jit.entry_x26 | cpubench-thumb | 5.694 -> 5.678 (-0.28%) | evidence/jit/x26-sim-cbthumb.txt |

The main finding: **on a game-sized code footprint the JIT's code is fetch-bound**. tests/bigcode's 1.7M JIT-code
instructions per frame take 6.2 of the main thread's 10.3 ms (IPC ~0.25), with 45k L1I misses a frame, half of them
missing the 512 KB L3 too; cpubench's JIT code runs at IPC ~1. That fits PERF.md's "0007 cut 40% of the JIT code's
instructions and nothing showed on HeartGold": for big code, bytes and layout matter, not instruction counts.
Rule of thumb from 0024: 4% less JIT code -> ~1.7% less main-thread time on bigcode.

### Exactness (all FAILS=0, toggle off vs on)
A clean build dir applies melonDS patches 0001-0024 and gives a binary byte-identical to the one the 0024 suite and
sim runs used (evidence/jit/clean-apply.txt).
evidence/jit/exact-thp.txt, exact-irqchain.txt (Ubuntu build), exact-irqchain-dev.txt (device build, with
irqmask x3), exact-x26.txt. Sabotage check: a build that also chained past *taken* IRQs was caught (FAILS=4,
evidence/jit/exact-irqchain-sabotage.txt).

### Device test commands (BananaDS tools/device/hgquick.sh; each line against a plain run of the same binary)
```
EXTRA="--set jit.hugepages=off" tools/device/hgquick.sh <bin>        # 0021: also check the log line
                                                                      #   "[jit] code memory at ..., N kB on huge pages" (N should be 32768)
EXTRA="--set jit.irqchain=off" tools/device/hgquick.sh <bin>         # 0023
EXTRA="--set jit.entry_x26=off" tools/device/hgquick.sh <bin>        # 0024
EXTRA="--set jit.hugepages=off --set jit.irqchain=off --set jit.entry_x26=off" tools/device/hgquick.sh <bin>   # all
```
Before the irqchain A/B, count how often HeartGold hits it: run the binary with hgquick's settings and
`MELONDS_JIT_EXITSTATS=1500 ... 2>&1 | sed -n '/JIT exit stats/,$p'` ("back for an IRQ masked" per frame, with
`--set jit.irqchain=off`; ~0 with it on). Then the 42-frame `rtc.fixed` HeartGold compare with everything on
(pictures must equal the build without these patches). For the huge pages, `perf stat -e iTLB-load-misses,
dTLB-load-misses` on and off shows the mechanism directly.

### Risks
- 0021: memory: none (all 32 MB were resident already); if the kernel can't find 2 MB pages it falls back to 4 KB.
- 0023: a masked IRQ now never ends a chain; anything that relied on the loop running between two blocks while an
  IRQ is masked would differ. Read: the loop does nothing there (TriggerIRQ returns at once with I set). Covered by
  tests/irqmask; not covered: DSi-only paths.
- 0024: anything entering a block other than ARM_Dispatch, a linked slot, DynExit, a self loop or the fault
  handler's context restore would run without x26. Read: there is nothing else.

### Dead ends (also in DEAD-ENDS.md)
- A lighter ARM_Dispatch (save only x29/x30, the call site an asm statement with x19-x28 clobbered): +3.2% slower
  (GCC spills the whole inlined loop). evidence/jit/deadend-litedispatch-sim.txt; patch in /home/user/perf/jit/dead/.
- FOLDSTORE (a folded conditional exit's dirty registers stored once): -0.4% of the register stores, dropped.
- The cycle budget in a register (PERF.md "What's left" #1): analysed, not built. ExitCheck is 230k of cpubench-thumb's
  2,747k JIT-code instructions (0.35 of 5.6 ms in the sim, 6%), not the 15-20% estimated; x19-x29 are all taken (no
  callee-saved register for the budget), so Timestamp would have to be materialised before every C call from JIT
  code; estimated net 2-4% on cpubench, less on games, with a high exactness risk.

### Next (JIT), in order
1. JIT code bytes for the L1I, measured on tests/bigcode with `/home/user/perf/jit/census.py <melonjit .code> <perf map>`
   (static census; blocks avg ~290 bytes: ALU 37%, guest register stores 12%, flags 10%, constants 7%, register loads
   7%, exits 10%). Candidates: Thumb shift flags (up to 11 instructions per flag-setting shift), the unaligned-load
   rotate sequence (5 instructions per word load), R15/CodeCycles stores, the conditional exits' cold way out of line.
2. The RET mispredict at every slice end (the ExitCheck stub returns to ARM_Ret instead of its caller; ~3.9k/frame
   on cpubench in the sim), and ARM_Dispatch's link-pending check (3 instructions per dispatch) moved to the learn path.
3. VRAM/OAM/palette stores inline through 0010's page table (PERF.md #3) after HeartGold's MELONDS_IOSTATS shows the
   rate (taken over by `core` on perf/core-jitmem at 16:56).
4. The cycle budget in a register (PERF.md "What's left" #1), only if a device profile shows the exit stubs high: it
   needs a callee-saved register (take x25 out of the guest-register allocator, 14 left) and Timestamp materialised
   before every C call from JIT code (or in shared call trampolines); see "Dead ends" above for the estimate.

## Part 2: the 2D engine (16:56-17:50, branch `perf/jit-2d` from `perf/render` @ eee302e; patches/jit-2d/)

Head `a8725d0`; `render` cherry-picked all three commits onto perf/render (94ee0ea, ea7732d, dc50d19).
Device build, rgds-sim v2 (v3 = SIMARGS=prfm=1 for prefetches), A/B one binary.

| Commit | Toggle (default) | What | Measured |
|---|---|---|---|
| 60a26a9 | `render.2d_conv16` (on) | ConvertRun16 (direct-colour bitmap BG rows): 16 pixels a NEON step; the layer's drawn/full test from the sources' bit 15 (callers only test acc/all for zero, and an opaque pixel always carries the non-zero flag) | membench st 1x: main 4.563 -> 4.478 ms/frame @1104 (-1.87%; ConvertRun16 175k -> 119k insns, 0.314 -> 0.240 ms); L5 2x dev: the 2D thread's 2d bucket -0.073 ms (evidence/jit/2d-conv16-sim-*.txt). With the toggle off the old loop costs 175k insns vs 155k in the base build (codegen moved): vs the base build the gain is ~0.05 ms |
| e3c69b1 | under `render.2d_fast` | FillLine (screen off / display mode 0) 64 bytes a NEON store | rare path, not measured |
| 60a26a9 + a8725d0 | `render.2d_prefetch16` (on) | ConvertRun16 prefetches its VRAM row and the next 512 bytes (a 256-pixel bitmap's next row, a DS line ahead) | v3 (timed PRFM): membench st 1x main 4.462 -> 4.372 (-2.01%; ConvertRun16 0.237 -> 0.133 ms; L3 misses -29%; only 83 of 3.3k prefetches/frame late) (evidence/jit/2d-prefetch16-sim-v3-membench.txt). **Rests on the model's PRFM (an assumption until the device); on by default only because a prefetch can't change results.** |

Exactness: ConvertRun16 random test against the old function, 200k cases, 0 mismatches, a sabotaged copy caught
(evidence/jit/2d-conv16-test.txt, 2d-conv16_test.cpp); render's matrix.sh (26 configurations: 2x device config x10
ROMs, savestate reload x5, 2x Gengis 0 threads, 1x, 1x with the 2D thread) FAILS=0 (evidence/jit/2d-matrix-conv16.txt).

What the model says about the output stage (WriteZip/WriteDouble/WriteLine/EmitA/B): with v2's write streaming the
2x rows' stores stream (no allocation), and the planar NEON kernels run at ~1 IPC with a near-minimal instruction
count: little left in the model (0.28 ms of the 2D thread on L5 2x). On the device the KMS dumb buffers are the
destination; if Rockchip maps them write-combined (frontend_kms.cpp: "dumb buffers are slow to read back") the cache
model doesn't apply: the stores are whole 64-byte lines already, and nothing reads the scanout buffers back.

Where ds2dtest's 2D time is (st 2x, an analysis build with the 2D functions noinline, evidence/jit/
2d-ds2dtest-st2x-noinline-attribution.txt; main thread 24.8 ms/frame @1104): DrawBG_Affine 4.41 ms (5.16M insns:
per-pixel rotscale), ApplyWindow 3.19 (render made it NEON at 17:33: render.2d_win_neon), DrawBG_Text 8bpp 2.18 (723k
insns: ~3 cycles/insn, misses on tile rows) and 4bpp 2.06, DrawSpriteEntry 1.90, EffectsTo 1.74, ResolveSprites 1.27,
ApplySpriteMosaicX 0.73. The BG reads miss to L3 (~80k loads/frame on the VRAM flats): tiles are evicted between
lines by the compositing at 2x.

Device A/B (1x and 2x; per-thread CPU time of `bananads-2d` and the emulator's thread with `top -H`):
```
EXTRA="--set render.2d_conv16=off" tools/device/hgquick.sh <bin>
EXTRA="--set render.2d_prefetch16=off" tools/device/hgquick.sh <bin>
EXTRA="--set render.2d_conv16=off --set render.2d_prefetch16=off" tools/device/hgquick.sh <bin>
```
(HeartGold may not use direct-colour bitmap BGs; the stress ROMs and membench do.)

Next (2D), by the attribution above:
1. DrawBG_Affine (per-pixel rotscale): NEON coordinates for 4-8 pixels a step, gathers for map and tile bytes, the
   tile row prefetched; it is the biggest single function on ds2dtest.
2. DrawBG_Text: gather the line's 33 map entries first, prefetch all tile rows (v3 shows whether it helps), then draw;
   or keep the decoded 8-pixel tile rows of the last line per BG while scroll, map and VRAM generation are unchanged.
3. Sprites (DrawSpriteEntry, ResolveSprites, ApplySpriteMosaicX: ~3.9 ms together on ds2dtest).

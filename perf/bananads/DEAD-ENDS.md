# Dead ends: read before starting an idea (append only)

## Already ruled out before this session (from BananaDS docs/PERF.md and docs/BANANALINUX.md at 00c34c78e8de)

- Inlining the JIT's exit checks into each block: 1% fewer instructions, but L1I refills +22% and iTLB walks +43%:
  slower on the A55 (PERF.md, patch 0001).
- `msr nzcv` + `b.cond` for a condition check: no better than melonDS's table (4.2 vs 3.2 cycles, A55).
- Continuing from `ARM_Ret` without returning to the loop (busy ARM7): exact but +144k instructions/frame: dropped.
- One ring buffer for the GX FIFO and pipe: exact with savestate conversion, but more instructions: dropped.
- Clearing GXSTAT's busy bits only after a command: not exact (a mid-run savestate differed): dropped.
- Deferring or batching sound mixing: not exact (channels read RAM at mix time while games refill played halves).
- Longer idle slices (`core.slice_idle` 1024/2048): no change in frame times.
- "The JIT cache resets during gameplay": false; profiles that showed `ResetBlockCache`/`munmap` sampled the exit.
- SuperDrastic measured on this hardware and removed: CPU pinning, `SCHED_FIFO` for the emulator's own threads,
  clock lock + PLL pacing, a fixed latch.
- `render.threaded=on` (melonDS's soft 3D thread) was slower on the device than off (48.6 vs 52.2 fps, L10).
- `MELONDS_JIT_LOOPREGS` (patch 0007): black screen in HeartGold on the device, cause unknown, off by default; the
  test ROMs don't reproduce it. Don't build on it without a repro.
- core.slice > 64 (`auto`, 256, 512) changes timing: Golden Sun: Dark Dawn drew no 3D world with `auto`; the default
  is back to 64 (BananaLinux run-bananads). Not a free lever.
- qemu instruction counts in JIT code did not carry over to HeartGold on the device (0007: -40% JIT-code
  instructions on cpubench, nothing measurable in HeartGold). Use the A55 model, and treat JIT-code-only wins with
  suspicion.

## This session
- (core, 16:20) `-fno-plt` for the emulator: the PLT stubs run 1.1k instructions/frame (0.3k indirect branches, 0
  mispredicted) on the device build, cpubench-thumb (rgds-sim per-address counts,
  /home/user/perf/core/runs/sim/dev-cpubench-thumb/f300.tsv, .plt 0x4054d0-0x406300): nothing to gain. The
  "bananads:?" 189k/frame in rgds-sim's libc/lib bucket is ARM_Dispatch/ARM_Ret (unsized .S symbols), not the PLT.
  The CMake option BANANADS_NOPLT stays (off) for completeness only.
- (render, 16:25) VBlank's polygon Y-sort as a stable counting sort instead of std::stable_sort (melonDS patch, exact): dsstress
  L10 -0.1% of the emulator's thread, geomtest 0 (under 64 polygons a frame). Dropped; evidence/render/qc-b5d-st-sort.txt,
  the patch in evidence/render/0060-geom-sort-dropped.patch.
- (core, 16:44) `BANANADS_HOTCOLD=ON` (`-freorder-blocks-and-partition` on top of the device PGO build): cpubench-thumb
  +0.31% ms/frame under rgds-sim (instructions +0.4%, L1I misses -6%, store misses +9%, bcond misses +5%);
  evidence/core/ab-hotcold-cpubench-thumb.txt. PGO's own .text.hot/.text.unlikely ordering already does the useful part.
- (jit, 16:30) A lighter ARM_Dispatch (save only x29/x30; the C++ call site an asm statement declaring x19-x28
  clobbered): +3.2% slower on cpubench-thumb in rgds-sim: GCC 14 then spills the whole inlined slice loop
  (+129k insns, L1I misses +30%). evidence/jit/deadend-litedispatch-sim.txt.
- (jit, 16:50) Storing a folded conditional exit's dirty guest registers once before the branch: only -0.4% of the
  guest register stores on tests/bigcode (the two-way duplicates are rare). Static census, /home/user/perf/jit/census.py.
- (core, 16:58) `BANANADS_HOT_OPT=-O3` on top of PGO (`-DBANANADS_PGO=prof-dev -DBANANADS_HOT_OPT=-O3` vs PGO alone):
  cpubench-thumb +1.4%, membench +1.5%, L5 0.0%, ipcrom -0.8% ms/frame (rgds-sim v1 st); evidence/core/ab-pgoO3-*.txt.
  With a profile, -O3 only adds code. Use PGO at Buildroot's -O2.
- (core, 17:41) The JIT's VRAM/OAM/palette stores fully inline at every store site (~36 instructions each): rgds-sim
  cpubench-thumb +1.7%, membench +0.1%, jit's bigcode +5.8% (code growth: L1I misses +46% on cpubench), and not exact on
  ds2dtest; evidence/core/ab-gfx1-*.txt. The shared JIT-emitted helpers (0041, jit.gfxstores) are the version that works.

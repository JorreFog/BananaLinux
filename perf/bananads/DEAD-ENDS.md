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

## Measured on the device after the session (RG DS Plus, HeartGold overworld; GUIDE-FOR-CLOUD-AGENTS.md)

- **The whole integrated series (23 patches):** +3% fps at 2x and +2% at 1x on HeartGold. The simulator said -2..-46%
  per frame on the test ROMs. Gains on test ROMs did not carry over to a game.
- **`render.3d_pipeline`:** 0% on HeartGold. The model's -27% of the emulator's thread doesn't show, because the
  thread's time goes elsewhere in a game.
- **"At 2x the 2D thread is the frame's limit" (rgds-sim, 16:47):** false on HeartGold. The emulator's thread is 99%
  busy, the 2D thread 31%, the Gengis workers 25% each. The 2D-only phase of the session (16:52 onward) was aimed at
  a thread that isn't the limit in a real game.
- **Building and tuning a simulator (rgds-sim) to decide what to optimise:** it was within 2% on one single-threaded
  number and wrong about which thread limits a game. Don't spend agent time on it again. Ask the local agent for
  per-thread CPU times and a profile of the busiest thread on a real game instead.
- **Still worth having from the session:** `render.2d_thread=on` at 1x (+21% base, +26% with the patches, already in
  the base code), the toggles themselves (all frame-exact on the device), and the device-faithful build
  (`build-a64-device.sh`).
- (session 2, geom) Taking a geometry command's middle parameters (a matrix's 2nd-15th) in a short loop in
  `GPU3D::Run`: exact, but gxstm `Run` -0.1%: the CPU paces the stream, ~2.7-5.3 FIFO entries arrive per `Run` call,
  so batching inside one call has little to batch.

## Session 3 (Mario Kart DS's race start)

- (geomlazy) **Lazy geometry** (running queued GX commands only when observed): not built. In MK's race start the GX
  FIFO is fed by DMA mode 7 every burst, the FIFO-empty IRQ's IF bit flips ~270 times a frame and `Run` is called
  ~2,180 times a frame (13 entries each); melonDS's timing depends on when `Run` runs and a command's cost on its
  result, so a lazy engine would have to replay every call point. At most ~1.5% of the thread (instructions).
  `session3/geomlazy-evidence/observers.md`.
- (geomlazy) The GX DMA block writer with each command's push/pop/test flags computed once per command: `DMA::Run9`
  +25k instructions a frame (register shuffling): dropped.
- (geomlazy) NEON lighting for MK: ~1 light per normal, ~60k instructions a frame to gain at most: not done.
- (mk2cpu) MK's SPU: MixChannelsFast's work for a due PCM16/ADPCM channel in one function per format, with `RunSamples`
  inline: exact but +112k instructions a frame (the loop -268k, the new functions +760k against `RunSamples`'s 380k);
  a due channel not marked lazy (`core.fast_due`): -15.6k, dropped. Every channel is due every Mix in MK: the cost
  is the per-channel state traffic. `blip_add_delta` is already NEON (GCC). `session3/mk2cpu-evidence/`.
- (mk2cpu) The RTC's 32,768 Hz event (548 a frame) and the SPU's can't be removed or merged exactly: each ends a slice,
  so the ARM9/ARM7 interleaving would move.
- (mk3geom) MK's polygon setup, vertex and lighting paths have no compact exact cut left (each region ≤170k a frame,
  spread out). A lighting result cache: consecutive normals never repeat; a 256-entry cache hits ~20%, which roughly
  pays for its own lookup. `AddCyclesT`'s bookkeeping can't stay in registers across `Run`'s loop (the out-of-line
  calls read it). `session3/mk3geom.md`, `session3/mk3geom-evidence/followup-polygon-vertex-lighting.txt`.
- (mk3jit) The exit check's timestamp address from x29 instead of three MOV/MOVKs (~18k checks a frame on MK):
  `NDS::ARM9Timestamp` is ~79 MB before `NDS::ARM9` (the NDS object holds the JIT's tables), out of one ADD's reach;
  loading the NDS pointer first is a dependent load. Not done.
- (mk3jit) A GX-only patched stub for the JIT's STRs to the GX FIFO port: after 0064 `SlowWrite9`'s GX case is first
  and direct; a special stub would save ~15-25 of ~50 instructions per STR (~2,076 a frame on MK): ~30-50k a frame,
  not worth the risk in the fault-patching code. `session3/mk3jit.md`.
- (mk3geom) Lighting on the geometry worker (0084 `render.geom_light`, not committed): about 0 on the emulator thread
  (lighting -639k, but handing each normal over costs ~100 instructions, +436k, and the input copies +103k), and not
  exact on gxstm. A worker thread for geometry is limited because the polygon's timing depends on its culling and
  clipping, which have to stay on the emulator thread (mk3geom.md's audit). WIP patch in `session3/mk3geom-evidence/`.

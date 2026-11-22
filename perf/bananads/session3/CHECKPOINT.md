# Session 3: where the work stopped, and where it started again

The local agent can test the work before the stop and after the restart separately. Both phases are on JorreFog/SuperDrastic.

| Phase | When (UTC) | Branch | Head | What it is |
|---|---|---|---|---|
| **Before the stop** | 2026-10-18 21:00 - 2026-10-19 00:00 | **`claude/bananads-perf-mk-checkpoint1`** | `8e98258` | **Frozen**: never updated again. Identical to `claude/bananads-perf-mkall` at the stop. CI artifact `bananads-aarch64-8e98258dd82b4191e932c75572a06f0183fd460b`. Contents: [README.md](README.md) |
| **After the restart** | 2026-10-19 from 09:55 | **`claude/bananads-perf-mk2`** | **`3a743f3`** (pushed 11:32) | `8e98258` plus the new commits only (8 cherry-picks, `-x`). CI device build passed: artifact `bananads-aarch64-3a743f32a9bd8726645980a61768ddf9bf2efa4e` (run 39245317154), one commit per change, each behind its own toggle. Built by integrating the two part branches below |
| part | from 09:55 | `claude/bananads-perf-mk2geom` | `58aa659` | the geometry engine's remaining cost on Mario Kart's race start, and session 2's GX FIFO store changes (memio) ported and checked on the game |
| part | from 09:55 | `claude/bananads-perf-mk2cpu` | `08d14fa` | the emulator thread's other costs on the race start: the SPU's per-event cost, the scheduler, IO slow paths from JIT code |
| **Phase 3** (the user's extra 4 hours) | 2026-10-19 from 12:05 | **`claude/bananads-perf-mk3`** | **`ea310c8`** (pushed 14:16) | `3a743f3` (mk2, now **frozen**) plus the new commits only (8 cherry-picks, `-x`), each behind its own toggle. CI device build passed: artifact `bananads-aarch64-ea310c8b9dd3e9d48ffbf793883fac125c993486` (run 39267415019). Built by integrating the two part branches below |
| part | from 12:05 | `claude/bananads-perf-mk3geom` | `649e008` | the geometry engine's own cost: `GPU3D::Run`, polygon setup, vertices, clipping |
| part | from 12:05 | `claude/bananads-perf-mk3jit` | `f1c467d` | the JIT code's cost on the race start and its slow paths (GX FIFO stores, CP15 MCRs, region lookups) |

No device numbers existed for the checkpoint when the work restarted (the user's call: continue without them).

## How to test the two phases

1. **Base vs before the stop.** Run `mkbase` (9aa478c, the profiler and no changes) against `mk-checkpoint1`: this is
   what session 3 delivered by 00:00.
2. **Before vs after.** Run `mk-checkpoint1` against `mk2`: only phase 2's work. Then `mk2` against `mk3`: only
   phase 3's.
3. **One new change at a time.** On the `mk2` binary, turn a new commit's toggle off (listed below at the hand-over),
   so one binary A/Bs every new change.
4. **Exactness**, as in [README.md](README.md): `exact.sh` on Golden Sun and HeartGold, for `mk2` as well.

## The new commits (after the restart)

All on `claude/bananads-perf-mk2`, in this order on top of `8e98258`. **Untested on device.** Gains are the share of
the emulator thread's cycles on MK's race start that each should save, from the device profile's shares and the qemu
instruction counts (which only show a path got cheaper). Full reasoning and checks: [mk2geom.md](mk2geom.md),
[mk2cpu.md](mk2cpu.md).

| Commit | melonDS patch | Toggle (default) | What | Expected saving |
|---|---|---|---|---|
| e1b44ed | 0063 | `render.gx_batch` (`on`) | the JIT's STMs into the GX FIFO handed over in one call (session 2's memio 0021) | MK ~0; for HeartGold, session 2's +1..2.5% fps if its STM share holds |
| 79500f2 | 0064 | `render.gx_str` (`on`) | the JIT's STRs to the GX ports before the region switch (session 2's memio 0022) | ~0.1% |
| c9b60df | 0065 | `render.geom_sort` (`on`) | VBlank's polygon Y-sort as a stable counting sort (qemu: the sort 108.7k -> 39.5k a frame) | 0.5..0.8% |
| 6195a3d | 0066 | `render.gx_dmarun` (`on`) | `DMA::Run9` runs the GX FIFO DMA's block path in a function of its own (qemu: -64.4k a frame in the DMA) | 0.4..0.6% |
| a2ecdb2 + b1776c6 | 0072 | `jit.wram7fast` (`on`, bit 8 of `MELONDS_MEMFAST`) | the JIT's slow accesses to the ARM7's WRAM skip the virtual call and the region switch (qemu: -162k a frame); b1776c6 fixes 0072's context after 0063-0065 | 0.6..0.8% |
| a4e0dab | - | `render.2d_ring` (`split`; the old layout is `shared`) | the 2D job ring's Head and Tail on cache lines of their own; qemu can't see it | 0.3..0.5%, **the least certain**: only the device can show it |
| 3a743f3 | - | `render.2d_test_delay` (`0`, tests only) | slows the 2D thread so the checks reach the ring's full path; leave it at 0 | - |

**Together: about 2..2.8% of the emulator thread on top of the checkpoint** (qemu: the thread 12.88M -> 12.71M
instructions a frame for the geometry part alone; `2d_ring` isn't visible to qemu). Checkpoint1's -11..-13% plus this
puts session 3 at roughly -13..-15% against `mkbase`. The brief's target was -15%.

**All new toggles off in one run** (the `mk2` binary then behaves like the checkpoint):
`--set render.gx_batch=off --set render.gx_str=off --set render.geom_sort=off --set render.gx_dmarun=off --set jit.wram7fast=off --set render.2d_ring=shared`

### Exactness of `mk2` (here)
- The MK scene's 8 frames and its audio are identical to the base with every toggle at its default and with every
  new toggle off: [mk2-exact.txt](mk2-exact.txt).
- Each part branch's own checks (main RAM difftest, full savestates where timing is touched, the test ROMs, jit's
  `exact-suite.sh`, a sabotage build per change) are in mk2geom.md and mk2cpu.md.

### Known gaps (check these on the device first if something looks off)
- 0063's sabotage is caught only by the `gxstm` test ROM, not by MK (MK hardly uses that path). Golden Sun and
  HeartGold's `exact.sh` is the real check.
- 0066 has two DMA branches the MK scene never takes. `exact.sh` on Golden Sun and HeartGold covers more DMA modes.
- `render.2d_ring`'s copy log full path isn't reached even with `render.2d_test_delay`; the job ring's full path is
  (its sabotage was caught).
- No device numbers exist for either phase.

### Dead ends after the restart
The SPU's per-event cost (two exact attempts, no gain) and merging the RTC's 32,768 Hz event (not exact: it moves
slice ends). See mk2cpu.md and DEAD-ENDS.md.

## Phase 3: the new commits (2026-10-19 12:05-15:10 UTC)

All on `claude/bananads-perf-mk3`, in this order on top of `3a743f3`. **Untested on device.** Hand-overs with the full
checks: [mk3geom.md](mk3geom.md), [mk3jit.md](mk3jit.md).

| Commit | melonDS patch | Toggle (default) | What | qemu (emulator thread, MK 1650-2150) | Expected saving |
|---|---|---|---|---|---|
| c112af1 | 0080 | `render.geom_queue` (`on`) | the geometry command loop reads its queue run by run; the pipe's refills worked out once after the loop | -295k (-2.3%) | 1..2% |
| 96c5e1f | 0081 | `render.geom_switch` (`on`) | one switch on each queued command; the full vertex command on a path of its own | -112k (-0.9%) | 0.5..1%, the least certain of the geometry ones |
| 863877a | 0082 | `render.gx_dmatime` (`on`) | the GX FIFO DMA's burst timing 4 words at a time | -102k (-0.8%) | 0.4..0.8% |
| 3521c94 | 0085 | `jit.hugepages` (`on`) | the JIT's code memory on 2 MB huge pages (~1,100 4 KB pages before); the startup log says how much got huge pages | none (qemu has no TLB) | 0.3..1.5% |
| 54c0f68 | 0086 | `jit.cp15nop` (`on`) | ~1,000 cache MCRs a frame (NitroSDK's flush loops) compiled to their cycles instead of the interpreter | -76k | 0.3..0.5% |
| 8ed1fe7 | 0087 | `jit.region_trio` (`on`) | two code regions kept to swap back to; region setups ~640 -> ~20 a frame | -34k | 0.2..0.4% |
| c0170e7 | 0088 | `jit.fastclz` (`on`) | the JIT emitter's leading-zero count by CLZ, not bit by bit (compile time only; emitted code byte-identical) | -38k | 0.1..0.3% |
| ea310c8 | 0083 | `render.geom_thread` (`on`) | a stored polygon's build on a worker thread (lock-free ring, drained at VBlank and savestates); all timing stays on the emulator thread | -166k on the thread; the worker runs ~507k | 1..2% **only if a core is free**: it can be slower. A/B it first |

**Together:** about **-3..-6%** of the emulator thread's cycles on top of mk2 (if `geom_thread` pays).

**Measured against the session's start** (one matched qemu run, the same script and frames,
[qcount-base-vs-mk3.txt](qcount-base-vs-mk3.txt)): the emulator thread **15.83M -> 12.23M instructions a frame
(-22.7%)** from `mkbase` to mk3. On the device, all three phases together are expected to save about **15-20%** of the
thread's cycles.

**All phase 3 toggles off in one run** (the mk3 binary then behaves like mk2):
`--set render.geom_queue=off --set render.geom_switch=off --set render.gx_dmatime=off --set render.geom_thread=off --set jit.hugepages=off --set jit.cp15nop=off --set jit.region_trio=off --set jit.fastclz=off`

### Exactness of `mk3` (here)
- The MK scene's 8 frames and audio are identical to the base with every toggle at its default and with the eight
  new toggles off: [mk3-exact.txt](mk3-exact.txt).
- The part branches checked, per change: main RAM every frame with `--regs --timing`, whole savestates at 1850 and
  2000, per-call traces of `Run` and of the GX DMA, the test ROMs, `exact-suite.sh`, and a sabotage build of each.

### Known gaps (check these on the device first)
- **`render.geom_thread`:**
  - whether a core is free (Gengis's bin threads and the 2D thread share the four A55s);
  - a sabotage that drops only the savestate drain is caught at 1850 but not at 2000; whether it's caught depends on
    the race between the threads.
- **0087's shared-WRAM mapping check:** its sabotage was not caught. `exact.sh` on Golden Sun and HeartGold is the check.
- **0082:** one sabotage is caught only by the randomized test, not by MK.
- **Test hook:** `MELONDS_GEOMSAB` (0083's sabotage switch) is read from the environment in the shipped code. Leave it
  unset.

### What phase 3 ruled out (DEAD-ENDS.md)
- **Polygon setup, vertices and lighting:** no compact exact cut is left.
- **Lighting on the worker (0084):** saves nothing and isn't exact on gxstm; the WIP patch is kept in the evidence.
- **The 8M target:** a geometry worker can't reach it, because a polygon's timing depends on its culling and
  clipping, which have to stay on the emulator thread. Even moving all of the math would leave ~9.2M.
- **JIT:** the GX-only STR stub, the exit check's timestamp address, and the shared stubs.

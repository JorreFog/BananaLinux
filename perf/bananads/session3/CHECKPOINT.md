# Session 3: where the work stopped, and where it started again

The local agent can test the work before the stop and after the restart separately. Both phases are on JorreFog/SuperDrastic.

| Phase | When (UTC) | Branch | Head | What it is |
|---|---|---|---|---|
| **Before the stop** | 2026-10-18 21:00 - 2026-10-19 00:00 | **`claude/bananads-perf-mk-checkpoint1`** | `8e98258` | **Frozen**: never updated again. Identical to `claude/bananads-perf-mkall` at the stop. CI artifact `bananads-aarch64-8e98258dd82b4191e932c75572a06f0183fd460b`. Contents: [README.md](README.md) |
| **After the restart** | 2026-10-19 from 09:55 | **`claude/bananads-perf-mk2`** | **`3a743f3`** (pushed 11:32) | `8e98258` plus the new commits only (8 cherry-picks, `-x`). CI device build passed: artifact `bananads-aarch64-3a743f32a9bd8726645980a61768ddf9bf2efa4e` (run 39245317154), one commit per change, each behind its own toggle. Built by integrating the two part branches below |
| part | from 09:55 | `claude/bananads-perf-mk2geom` | `58aa659` | the geometry engine's remaining cost on Mario Kart's race start, and session 2's GX FIFO store changes (memio) ported and checked on the game |
| part | from 09:55 | `claude/bananads-perf-mk2cpu` | `08d14fa` | the emulator thread's other costs on the race start: the SPU's per-event cost, the scheduler, IO slow paths from JIT code |

No device numbers existed for the checkpoint when the work restarted (the user's call: continue without them).

## How to test the two phases

1. **Base vs before the stop.** Run `mkbase` (9aa478c, the profiler and no changes) against `mk-checkpoint1`: this is
   what session 3 delivered by 00:00.
2. **Before vs after.** Run `mk-checkpoint1` against `mk2`: only the new work.
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

# Session 3 (2026-10-18, 21:00-00:10 UTC): Mario Kart DS's race start

This follows [BRIEF.md](BRIEF.md) and [../GUIDE-FOR-CLOUD-AGENTS.md](../GUIDE-FOR-CLOUD-AGENTS.md). The work is on
the emulator thread only, with one toggle per change, each exact on the game itself. Nothing here is measured on a
device. Every gain below is an estimate with its reasoning, taken from the device profile's shares.

The work used the user's Mario Kart DS (USA) ROM (never committed or pushed) and a scene script that reaches
Figure-8 Circuit's countdown from a fresh save: [mkside-evidence/mk-scene.sh](mkside-evidence/mk-scene.sh). Without the
user's save, the script's frames are not the device's (the countdown here is 1780-1965; the heavy window is 1680-2100).
The brief's `rtc.fixed=on` is not parsed (it needs a date), so the script uses `2026-10-18 12:00:00`.

## Test this first: everything together

**`claude/bananads-perf-mkall`**, head `8e98258`, on JorreFog/SuperDrastic. It holds every commit below on
`claude/bananads-perf-mkbase`, cherry-picked one per original commit. The CI artifact is
`bananads-aarch64-8e98258…` (the `bananads-device` workflow on push).

Exactness (checked by the coordinator after a container restart lost the agents' last runs): the Mario Kart scene's 8
frames (1500, 1740, 1800, 1860, 1920, 1980, 2040, 2100) and its audio are identical to the base. That holds with every
toggle at its default and with every toggle off. `mkside` checked full savestates at 1850 and 2000 for its part, and
`geomlazy` for its part. A whole-savestate compare on `mkall` itself didn't finish before the restart.

**Expected on the race start:** about **-11 to -13% of the emulator thread's cycles**. That is geometry's ~-8.5% plus
mkside's -2.5..-4% plus the JIT's +0.2..0.5%; the parts touch different code, so they mostly add. Under qemu, geomlazy
and mkside together took the thread from 15.78M to 13.06M instructions a frame (-17%) over the heavy window. The
brief's target was -15%, so this is likely short of it. The slowest 100-frame window at 1992 MHz (53.7 fps) would go to
about 60.

## The parts (one branch each, so the device can tell which mattered)

| Branch | Head | Commits (toggle, default) | Expected (agent's reasoning) |
|---|---|---|---|
| `claude/bananads-perf-geomlazy` | 12f4a5a | 0050 `render.geom_batch` (on): the GX FIFO's DMA/IRQ checks once per `Run`; 0060 + 0062 `render.gx_dmafast` (on): the GX FIFO DMA's block unpacked in registers, burst timing in registers; 0061 `render.geom_fifo` (on): `Run` reads pipe and FIFO as one queue; difftest `--input/--from/--fresh-save/--regs/--timing`; a geomtest FIFO-IRQ phase | **-8.5%** (range -6..-9%): geometry + GX DMA 9.64M -> 7.05M instructions a frame under qemu |
| `claude/bananads-perf-mkside` | db1eb04 | 0030 `jit.slice_chain` + 0031 `core.halt_skip` (session 2's loop, on); 0070 `core.fast_samples` (on): ADPCM/PCM16 samples with the channel state in registers; `render.2d_kick` 64 (was 16): the 2D thread woken 4x less often (this was `pthread_cond_signal`); 0071 `jit.region_pair` (on) | **-2.5..-4%** together (+1..2.5, +0.8, +0.6, +0.4) |
| `claude/bananads-perf-mkjit` | f1553af | 0040 `jit.entry_x26` (on), 0041 `jit.dyncold` (on), 0042 `jit.flagstub` (**off**: its mistakes aren't caught even on MK) | **+0.2..0.5%**: JIT blocks -6.4% (-10% with flagstub) on the MK scene; by the profile's L1I/iTLB arithmetic, fetch is ~6-8% of the thread |

Hand-overs with the full commands and checks: [geomlazy.md](geomlazy.md), [mkside.md](mkside.md), [mkjit.md](mkjit.md).

## Device runs (the brief's section 5), in order

1. **`mk.sh` at 1992 MHz, frames 1650-2150 and 1700-2700:** base (`mkbase`'s artifact has the profiler and no
   changes) vs `mkall`. Report the slowest 100-frame window.
2. The same with **`prof.hz=1000`** (all these branches have the profiler): the new profile of the emulator thread for
   the next session.
3. **`kmsmk.sh`:** `kms.stats=60` through the countdown, and the clock the governor settles at. Watch for frame
   repeats: `render.2d_kick` is the one change that can touch pacing.
4. If `mkall` gains, the three part branches the same way, to see which one mattered.
5. **`exact.sh` on Golden Sun: Dark Dawn and HeartGold** with `mkall` (all defaults, then all toggles off), against
   the base. Neither game is here; Golden Sun is the one that catches IPC and timing mistakes.

To turn everything off in one run, for an A/B within one binary:
`--set render.geom_batch=off --set render.gx_dmafast=off --set render.geom_fifo=off --set jit.slice_chain=off --set core.halt_skip=off --set core.fast_samples=off --set render.2d_kick=16 --set jit.region_pair=off --set jit.entry_x26=off --set jit.dyncold=off`.

## What the next session should know

- **Lazy geometry won't pay off for this game.** MK's GX FIFO is fed by DMA in bursts, its empty IRQ's IF bit flips
  ~270 times a frame and `Run` is called ~2,180 times a frame. Lazy geometry is worth ≤1.5% here (DEAD-ENDS.md).
  What's left in geometry is spread out: polygon setup and the vertex transform.
- **Compare whole savestates for timing changes.** A DMA timing mistake left main RAM, the registers and the geometry
  state identical at every frame end; only the savestate differed (geomlazy.md).
- **If the device shows nothing for the JIT footprint changes, stop that line of work for MK** (mkjit.md).
- **One unexplained difference:** 4 bytes of the SPI firmware section in geomtest's savestate differ from the old base
  build whatever the toggles (geomlazy.md). It's probably unrelated, but nobody traced it.
- **`jit.flagstub` stays off** until a test can catch its mistakes.

# Session 3: where the work stopped, and where it started again

The local agent can test the work before the stop and after the restart separately. Both phases are on JorreFog/SuperDrastic.

| Phase | When (UTC) | Branch | Head | What it is |
|---|---|---|---|---|
| **Before the stop** | 2026-10-18 21:00 - 2026-10-19 00:00 | **`claude/bananads-perf-mk-checkpoint1`** | `8e98258` | **Frozen**: never updated again. Identical to `claude/bananads-perf-mkall` at the stop. CI artifact `bananads-aarch64-8e98258dd82b4191e932c75572a06f0183fd460b`. Contents: [README.md](README.md) |
| **After the restart** | 2026-10-19 from 09:55 | **`claude/bananads-perf-mk2`** | (filled in at the hand-over) | `8e98258` plus the new commits only, one commit per change, each behind its own toggle. Built by integrating the two part branches below |
| part | from 09:55 | `claude/bananads-perf-mk2geom` | | the geometry engine's remaining cost on Mario Kart's race start, and session 2's GX FIFO store changes (memio) ported and checked on the game |
| part | from 09:55 | `claude/bananads-perf-mk2cpu` | | the emulator thread's other costs on the race start: the SPU's per-event cost, the scheduler, IO slow paths from JIT code |

No device numbers existed for the checkpoint when the work restarted (the user's call: continue without them).

## How to test the two phases

1. **Base vs before the stop.** Run `mkbase` (9aa478c, the profiler and no changes) against `mk-checkpoint1`: this is
   what session 3 delivered by 00:00.
2. **Before vs after.** Run `mk-checkpoint1` against `mk2`: only the new work.
3. **One new change at a time.** On the `mk2` binary, turn a new commit's toggle off (listed below at the hand-over),
   so one binary A/Bs every new change.
4. **Exactness**, as in [README.md](README.md): `exact.sh` on Golden Sun and HeartGold, for `mk2` as well.

## The new commits (after the restart)

(Filled in at the hand-over: commit, toggle, default, expected gain on MK's race start with the reasoning, and the
exactness run.)

# Prompt for the local agent (RG DS / RG DS Plus): test session 3 on the device

You have the handheld; the cloud agents didn't. Session 3 sped up BananaDS's emulator thread on Mario Kart DS's
race start in three phases. **Nothing has been measured on a device yet**: every gain in the docs is an estimate
from the device profile and qemu instruction counts. Your job is to measure it, check it stays exact on the games
the cloud didn't have, and write the results down for the next session.

## Read first
On JorreFog/BananaLinux, branch `claude/adoring-dijkstra-daocav` (PR #16), in `perf/bananads/`:
1. `session3/CHECKPOINT.md`: the three phases, their branches, CI binaries, every new toggle and an "all off"
   string per phase. **Start here.**
2. `session3/README.md`: phase 1, and the device runs (its "Device runs" section: `mk.sh`, `prof.hz`, `kmsmk.sh`,
   `exact.sh`).
3. `GUIDE-FOR-CLOUD-AGENTS.md` (your own rules), then the hand-overs only when a result needs explaining:
   `mk2geom.md`, `mk2cpu.md`, `mk3geom.md`, `mk3jit.md`.

## The binaries (JorreFog/SuperDrastic, CI workflow `bananads-device`, artifact `bananads-aarch64-<full sha>`)

| Name | Branch | Head |
|---|---|---|
| base | `claude/bananads-perf-mkbase` | its CI artifact: the profiler, no changes |
| phase 1 | `claude/bananads-perf-mk-checkpoint1` | `8e98258` |
| phase 2 | `claude/bananads-perf-mk2` | `3a743f3` |
| phase 3 | `claude/bananads-perf-mk3` | the head in CHECKPOINT.md (final after 2026-10-19 ~15:45 UTC) |

Use the same save, the same scene (Figure-8 Circuit's race start) and the same clock for every run.

## What to run, in order
1. **Speed, one phase at a time.** `mk.sh` at 1992 MHz over frames 1650-2150 and 1700-2700:
   - base vs phase 1;
   - phase 1 vs phase 2;
   - phase 2 vs phase 3.
   Report fps and the slowest 100-frame window. Run each pair at least twice; say how much runs vary.
2. **The new profile:** phase 3 with `--set prof.hz=1000` over the race start. Commit the profile text next to
   `session3/prof-mk-race-start.txt`. It's the next session's starting point.
3. **Pacing:** `kmsmk.sh` with `kms.stats=60` through the countdown, on phase 3. Report the clock the governor
   settles at and any frame repeats.
4. **Exactness on the games the cloud didn't have:** `exact.sh` on Golden Sun: Dark Dawn and on HeartGold. Run it
   with phase 3 on all defaults, then with every toggle off, each against the base. The cloud's checks missed some
   bugs in three changes, so these two games are the real check: 0063 `render.gx_batch`, 0066 `render.gx_dmarun`
   and 0087 `jit.region_trio`.
5. **If phase 3 gains, A/B its riskiest toggles one at a time** on the phase 3 binary (same code layout):
   - `--set render.geom_thread=off`: a worker thread for the geometry. It only pays if a core is free; Gengis's bin
     threads and the 2D thread share the four A55s. It may be slower; report both ways.
   - `--set jit.hugepages=off`: check the startup log line `[jit] code memory at ..., N kB on huge pages`. Compare
     `prof.pmu`'s iTLB misses per 1000 instructions on and off.
   - `--set render.2d_ring=shared` (phase 2): only the device can show cross-core cache traffic.
   - `MELONDS_JIT_NOEXITPC=1`: a device-only question from mk3jit.md (an L1D miss in the JIT's exit check).
6. If a phase loses or breaks something, bisect it with its toggles (CHECKPOINT.md lists them) before anything
   else.

## Rules
- Don't merge, release or tag anything. Don't push to the session's branches; write results in BananaLinux only.
- Write numbers only with the command line and the raw output next to them.
- Don't touch audio rate control.

## Report back
Write `perf/bananads/session3/DEVICE-RESULTS.md`, then commit and push it to `claude/adoring-dijkstra-daocav`:
- each A/B's fps and slowest window, with the run-to-run spread;
- `exact.sh`'s results per game and setting;
- the toggles that helped, did nothing or hurt;
- the new profile's top 20;
- your verdict per phase: keep, keep with a toggle off, or drop.
Raw logs go in `session3/device-evidence/` (text only).

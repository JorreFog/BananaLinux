# BananaDS performance: how cloud agents should work

Written 2026-10-18, after the four-agent session in BananaLinux PR #16 (`perf/bananads/`) was measured on a real RG DS
Plus. Read this before starting any BananaDS speed work.

## 1. What the last session taught us

PR #16's 23 patches were tested on the Plus (BananaOS, HeartGold overworld, real save, 600 measured frames):

| | Simulator said | Device measured |
|---|---|---|
| Everything together, 2x | -2% to -46% per frame | **+3% fps** (105.5 -> 108.6 at 1992 MHz; 69.9 -> 71.4 at 1104) |
| Everything together, 1x | -6% to -10% | **+2% fps** |
| `render.3d_pipeline` (the "biggest" change, -27% of the emu thread) | big | **0%** |
| `render.2d_thread=on` at 1x (a one-line default, already in the base code) | -36% of the emu thread | **+21% base, +26% with the patches** |
| "At 2x the 2D thread is now the limit" | yes | **No. The emulator thread is 99% busy, the 2D thread 31%, Gengis workers 25% each** |

All patches were frame-exact on the device, so the work was safe but mostly aimed at the wrong thread. The biggest
win of the day was a default nobody had flipped, and a day of agent time went into building a simulator.

The lessons:

1. **Test ROMs are not games.** ds2dtest, dsstress and membench stress the 2D engine and the renderers. HeartGold (and
   most real games) is bound by the **emulator thread**: the JIT's code, memory/IO slow paths, 3D geometry, the
   scheduler and the SPU. A gain on a test ROM says nothing until a game shows it.
2. **A model is not a device.** rgds-sim got within 2% on one single-threaded number and was wrong about which thread
   limits a real game. Don't build or tune simulators; spend the time on code and ask for device numbers.
3. **Find the bottleneck before optimising.** Look at the per-thread CPU time on a real game first. Only work on the
   thread that is at ~100%. Making a 31%-busy thread faster changes nothing.
4. **Check defaults and threading first.** A setting flip (+21-26%) beat 23 patches (+2-6%).

## 2. What to optimise (in order)

The target is **BananaOS on the RG DS / RG DS Plus, the KMS frontend, 2x, real games at a stable 60 fps at the lowest
clock** (lower clock = longer battery, cooler). Headless fps at a fixed clock is the proxy; the cpugov clock a game
settles at is the real score.

Known numbers (Plus, HeartGold overworld, PR #16 integrated build, headless, 2 Gengis threads):

| Setup | 1104 MHz fps | Emulator thread | 2D thread | Gengis workers |
|---|---|---|---|---|
| 2x | 71.9 | **99%** | 31% | 25% + 25% |
| 1x + `render.2d_thread=on` | 74.4 | **99%** | 23% | 9% + 9% |

An older device profile of the emulator thread (HG 2x at 1104 MHz, 2026-10-12, `docs/PERF.md`): JIT-generated code
32%, memory/IO slow paths 18% (SlowBlockTransfer9 = STMs into the GX FIFO 5.7%), 3D geometry 13%, scheduler/RunFrame
10%, JIT dispatch and exit stubs 7%, SPU 6.6%.

So the priorities are:

1. **The emulator thread.** The slow memory paths (GX FIFO stores, IO register accesses that leave the JIT), the
   geometry engine, the JIT's exits and dispatch, the scheduler's per-event cost, and the SPU's mixing. Move work off
   it (to idle cores) where it's exact to do so.
2. **Defaults.** Make `render.2d_thread=on` the default at 1x (exact, +21%). Look for more settings like it.
3. **Heavier games.** HeartGold already runs at 72 fps at 1104. Find the games that don't make 60 at 1104 (3D-heavy
   ones: Metroid Prime Hunters, Golden Sun: Dark Dawn, Mario Kart DS races) and profile those.
4. Only then the 2D engine or the renderers, and only for a game where that thread is the limit.

## 3. How to work

- **Start from a device profile, not an idea.** Ask the local agent (the one with the handheld) for per-thread CPU
  time and a profile of the busiest thread on the target game. Pick the top symbols and work on those.
- **One change, one toggle, one exactness check.** Keep the rule from PR #16: each change has a setting whose "off" is
  the old behaviour, and keeps frames, RAM and sound identical. That part worked well.
- **Rank changes by expected gain on a real game, with a reason.** For example: "symbol X is 9% of the emulator thread
  in HG; this removes ~half of it, so expect +4%". The local agent measures the top few first.
- **Small batches.** Hand over 3-6 changes at a time with device numbers wanted, not 23 at once. The local agent's
  benchmarks are short on purpose (see section 4), and a big batch hides which change mattered.
- **Push a SuperDrastic branch**, not patch series inside BananaLinux. One branch (e.g. `claude/bananads-perf-<topic>`)
  based on the commit `buildroot/package/bananads/bananads.mk` pins, with one commit per change.
- **Build like the device.** BananaLinux builds with Buildroot's GCC 14.3 at -O2. If the CI can produce aarch64
  `bananads` binaries for the branch (GCC, as Buildroot does), attach them as artifacts: then the local agent can test
  PGO and other build options directly (it has only a clang cross-compiler, so it can't use a GCC PGO profile).
- **Don't count off-device numbers as results.** qemu instruction counts are fine for spotting regressions in a tight
  loop; they are not results. Write "untested on device" until the local agent has measured it.
- **Not just fps.** Frame pacing (repeats and drops on the panels, `kms.stats`), audio (no crackles, no pitch
  changes), input latency and the clock the cpugov settles at all count. A change that adds fps but misses refreshes
  is a regression.

## 4. How things get tested on the device

The local agent runs these on the Plus (192.168.50.102); the kit is in `/data/bananads-dev`:

- `hgquick.sh <bin>...`: HeartGold overworld, 600 measured frames, `CLK=` sets the clock (default 1992000),
  `EXTRA="--set ..."` passes settings. About 15-35 s per run. A/B lines alternate base and new, two runs each.
- `exact.sh <tag> <bin> [--set ...]`: 42 frames (every 50th of 2100) with the clock fixed; md5 lists in `ex/` to
  compare. Run it for every build and every toggle that changes a default.
- `threads.sh <bin> [--set ...]`: CPU % per thread over an 8 s window at 1104 MHz. Use it to find the limiting thread.

Rules the agents' scripts must follow:

- **Benchmarks must prove they measure the scene they claim.** On 2026-10-18 HeartGold "ran" at 420 fps: the save
  wasn't found (BananaDS looks for `<ROM name>.sav`) and the game sat in its new-game tutorial. Also, the repo's
  `tools/device/walk.txt` has `eft:` instead of `:left:` (a zsh `:l` mangling), so the walk never walked left. Every
  benchmark should dump one frame at the start of the measured window and compare it with a known hash, and print
  fps sanity bounds.
- **Keep them short.** The user's rule: a few hundred frames, one or two configurations per run. Larger suites
  (many configurations, long walks) need the user's OK first, with an estimated time.
- **Fixed clock for A/Bs** (1104 MHz is the one that matters; 1992 for headroom), and compare builds from the same
  compiler only (local clang builds are ~4% slower than GCC builds).
- `/data` is exFAT (no symlinks; `scp` needs `-O`); `/run` is a 490 MB tmpfs, so frame dumps go to `/data`.
- BananaOS has **no `perf`**. To get symbol profiles, the next image (or a dev tarball) should ship a static aarch64
  `perf`. Until then, per-thread `/proc` times and the code's own counters (`MELONDS_JIT_EXITSTATS`, `kms.stats`,
  `audio.stats`, `cpugov.log`) are the tools.

## 5. Games on the Plus for testing

Dragon Quest Monsters: Joker, Golden Sun: Dark Dawn, Mario Kart DS, Metroid Prime Hunters, Pokémon HeartGold,
Pokémon Black 2, Pokémon Platinum (in `/data/roms/nds`). Cloud agents don't have these ROMs, which is one more reason
the device measures and the agents propose. Useful scenarios to script next: an MKDS race, MPH in a room with enemies,
Golden Sun's 3D world map, a Black 2 town (3D + 512 MB ROM).

## 6. Checklist for a new session

1. Read this file and `perf/bananads/DEAD-ENDS.md`.
2. Ask for (or read) the latest device per-thread times and profile for the target game.
3. Work only on the thread that is at ~100%.
4. Each change: a toggle, an exactness check, an expected device gain with its reasoning.
5. Push a SuperDrastic branch from the pinned commit; list the device A/B commands, most promising first.
6. Wait for device numbers before stacking more changes on a direction.

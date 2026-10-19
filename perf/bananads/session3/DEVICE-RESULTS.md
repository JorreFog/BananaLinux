# Session 3 on the device: Mario Kart DS's race start on the RG DS Plus's panels (2026-10-19, 14:45-16:00 UTC)

Measured by the local agent on the RG DS Plus (BananaOS 0.4.0, 192.168.50.102). The RG DS (non-Plus) was off the
network all afternoon: nothing here is from it.

**The user's rule for this session: no headless tests.** Every number below comes from the game on the panels through
the real launcher (`run-bananads`, the KMS frontend, the card's own `bananads.conf`: 2x, `ds-grid-2x` shader,
`core.slice=64`, cpugov on with an empty memory), started by the menu's launch hook (`/run/banana/launch`, as A on the
game in the library), with `BANANADS_KMS_STATS=60`, `BANANADS_CPUGOV_LOG=on` and `BANANADS_AUDIO_STATS=on` through
`/data/banana/bananads.env`. The `mk.sh`/`prof.hz`/`exact.sh` headless runs the prompt asked for were started, then
stopped at the user's instruction (five partial runs, not used: their logs are in `device-evidence/headless-stopped/`).
Consequences:

- **No frame-exactness compare was run.** `exact.sh` (frame dumps, audio dumps) is a headless-only facility. The
  cloud's checks stand on their own; the games the cloud didn't have (Golden Sun: Dark Dawn, HeartGold) are
  **unchecked on the device**. Nothing visibly wrong was seen in the Mario Kart runs (the race ran, the countdown and
  the race's audio played; the logs show no errors), but that is not an exactness check.
- The headless-only questions (the slowest 100-frame window at a pinned clock, `prof.pmu` over fixed frames) are
  answered on the panels instead: each run's one-second windows, repeats and drops, the clock the governor went
  to, and a `prof.hz` profile of the emulator thread while the race starts.

## The binaries

The CI `bananads-device` artifacts are headless-only builds (KMS and ALSA off), so they can't run on the panels. The
four phases were built here with BananaLinux's own toolchain (Buildroot `output/rgds-plus/host`, GCC 14.3) exactly as
`buildroot/package/bananads/bananads.mk` configures the package (pristine melonDS `906e9eb` + the branch's
`patches/melonds/`, `-DBANANADS_KMS=ON -DBANANADS_ALSA=ON -DBANANADS_LTO=ON -DBANANADS_CPU=cortex-a55`, gcc-ar):

| Name | SuperDrastic branch | Head | melonDS patches | sha256 (first 16) |
|---|---|---|---|---|
| base | `claude/bananads-perf-mkbase` | `9aa478c` | 20 | `48054958ae12ae37` |
| phase 1 | `claude/bananads-perf-mk-checkpoint1` | `8e98258` | 31 | `1b415d285cdfcaed` |
| phase 2 | `claude/bananads-perf-mk2` | `3a743f3` | 36 | `f0f46f993b05c40b` |
| phase 3 | `claude/bananads-perf-mk3` | `ea310c8` (the final head: the cloud agents finished at 15:10 UTC and the branch did not move) | 44 | `7510c0172c0146f2` |

Same compiler and flags for all four, so they compare with each other (not with yesterday's numbers from the 0.4.0
image's binary or the CI artifacts).

## The run

`kmsmk4.sh <tag> <bin> 40 [KEY=value...]` (in `device-evidence/`): the menu starts the game with `<bin>` bind-mounted
over `/usr/bin/bananads`, a copy of the user's save, an empty cpugov memory; 5 s after the process appears, A is
tapped every 0.75 s for 25 taps (title -> Single Player -> Grand Prix -> Mario -> kart -> Mushroom Cup -> Figure-8
Circuit), then A is held 40 s (gas through the intro, the countdown and the first ~25 s of the race); the game is then
quit with SIGTERM. One run is about 64 s of play: windows 1-16 are the menus, the race intro loads around window 18,
**the heavy part (intro camera, countdown, the start) is windows 21-30**, the race holds a steady 60 after. Each run
waits for the SoC to be under 50 °C before starting (it ends at 57-62 °C; the Plus's thermal trip is 70 °C, never
reached). Runs alternate builds so drift (heat, the card) hits all of them.

Columns: the one-second windows under 59 fps shown, the frames lost in them (sum of 60 - fps), the panels' repeated
refreshes ("refreshes without a new frame") and dropped frames over the whole run, the audio servo's short chunks
(`[alsa]` 10 s lines), the worst window from the intro on, the clock steps cpugov made, the SoC temperature at the
start and the end, and the shown fps of windows 17-33.

## 1. The phases (the prompt's step 1), run order base1, p1-1, p2-1, p3-1, base2, p1-2, p2-2, p3-2, base3, then p2-3 and p3-3 in the second sequence

| run | env | windows | under 59 | frames lost | repeats | drops | short audio chunks | worst window fps | cpugov steps (MHz) | SoC °C | huge pages | windows 17-33 (fps) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| base1 |  | 62 | 11 | 78 | 84 | 2 | 153 | 47.71 | 1800 | 39-55 | - | 60 50 60 60 60 58 54 53 57 56 48 48 51 60 60 60 60 |
| base2 |  | 62 | 12 | 98 | 117 | 2 | 213 | 44.15 | 1800 1608 | 49-59 | - | 60 51 60 60 60 53 57 47 59 55 56 44 53 51 51 60 60 |
| base3 |  | 62 | 14 | 110 | 126 | 3 | 177 | 44.55 | 1800 1992 | 50-62 | - | 57 59 60 60 60 45 55 45 52 53 47 52 53 57 52 56 60 |
| p1-1 |  | 63 | 5 | 39 | 40 | 4 | 82 | 47.35 | - | 46-57 | - | 56 60 60 60 60 54 60 60 60 61 47 55 60 60 60 60 60 |
| p1-2 |  | 63 | 5 | 41 | 39 | 3 | 101 | 45.87 | 1800 | 49-60 | - | 46 60 60 60 56 58 60 60 60 60 51 60 60 60 60 60 60 |
| p2-1 |  | 63 | 6 | 39 | 40 | 3 | 94 | 52.92 | - | 50-58 | - | 60 55 60 60 60 59 59 60 60 60 59 53 58 60 60 60 60 |
| p2-2 |  | 63 | 4 | 35 | 33 | 2 | 90 | 50.69 | 1800 1992 | 49-60 | - | 51 60 60 60 60 59 60 60 60 60 51 59 60 60 60 60 60 |
| p2-3 |  | 63 | 7 | 39 | 36 | 6 | 73 | 48.68 | 1800 1608 | 49-61 | - | 55 60 60 60 60 57 60 60 60 57 49 57 58 60 60 60 60 |
| p3-1 |  | 63 | 9 | 61 | 69 | 2 | 67 | 51.74 | 1800 1992 | 49-60 | yes | 58 60 60 61 60 52 53 54 54 53 52 56 59 60 60 60 60 |
| p3-2 |  | 62 | 11 | 77 | 91 | 2 | 134 | 39.95 | 1800 1608 | 50-61 | yes | 58 60 60 60 60 54 58 56 52 53 40 52 57 60 60 60 55 |
| p3-3 |  | 63 | 11 | 69 | 79 | 3 | 121 | 49.85 | 1800 1992 | 49-62 | yes | 60 57 60 59 60 58 57 55 55 51 51 50 55 58 60 60 60 |

Means over the runs (frames lost in windows under 59, and the shown fps of windows 17-33):

| Build | frames lost per run | mean | mean fps, windows 17-33 |
|---|---|---|---|
| base | 78, 98, 110 | **95** | 59 53 60 60 60 52 55 48 56 55 50 48 52 56 54 59 60 |
| phase 1 | 39, 41 | **40** | 51 60 60 60 58 56 60 60 60 60 49 57 60 60 60 60 60 |
| phase 2 | 39, 35, 39 | **38** | 55 58 60 60 60 58 60 60 60 59 53 56 59 60 60 60 60 |
| phase 3 (as shipped) | 61, 77, 69 | **69** | 58 59 60 60 60 55 56 55 53 53 48 53 57 59 60 60 58 |
| phase 3 with `render.geom_thread=off` (4 runs, section 2) | 27, 33, 31, 31 | **31** | 56 59 60 60 60 54 60 60 60 60 55 58 60 60 60 60 60 |

What the race start looks like now: the base dips for the whole intro and countdown (windows 18-31 at 48-56 fps,
84-126 repeated refreshes a run). Phases 1-3 (worker off) leave three short dips: the track load (window 17-18, one
window at 51-56), one window at the start of the countdown (window 22, ~54) and the start itself (windows 27-28,
55-58), ~25 repeats a run. The base's long dip is gone; the three that remain are each one second.

**Run-to-run spread:** the same configuration repeats within about ±3 frames lost when it's good (phase 3 + worker
off: 27-33 over four runs; phase 2: 35-39 over three) and ±16 when it's bad (base 78-110). The toggle runs in
section 2 spread 20-42 with two runs each, so a difference under ~15 frames lost per race start is not resolvable
with two runs; nothing in section 2 is bigger than that.

**The clock:** cpugov steps 1992 -> 1800 MHz once the race is steady in every build (the "1800" in the steps column),
and within the 40 s it sometimes steps again to 1608 or back up to 1992 after a late frame: no build settles lower
than another in a run this short. The three runs with no step at all (p2-1, the two `2d_ring=shared` runs) stayed at
1992 MHz the whole time, which is the governor's 10-s rule meeting a late frame, not a property of those builds.

**Audio:** the servo's short chunks follow the dips (base 150-210 a run, the good configurations 30-80): fewer dips,
less pitch bending. Not a separate effect.

## 2. Phase 3's toggles, one at a time (the prompt's step 5), on the phase 3 binary with the worker off (`p3x`)

Run order: p3-gtoff-1, p3-3 (default, in section 1), p3-gtoff-2, p2-3 (section 1); then p3x-1, hpoff-1, ring-1, noexitpc-1, p3x-2, hpoff-2, ring-2, noexitpc-2, alloff-1.

| run | env | windows | under 59 | frames lost | repeats | drops | short audio chunks | worst window fps | cpugov steps (MHz) | SoC °C | huge pages | windows 17-33 (fps) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| p3-gtoff-1 | | 63 | 4 | 27 | 24 | 6 | 55 | 50.57 | 1800 1992 | 50-61 | yes | 56 60 60 60 60 51 60 60 60 60 57 60 60 60 60 60 60 |
| p3-gtoff-2 | | 63 | 4 | 33 | 26 | 2 | 79 | 51.43 | 1800 1992 | 49-61 | yes | 51 60 60 60 60 52 60 60 60 60 55 60 60 60 60 60 60 |
| p3x-1 | | 63 | 6 | 31 | 25 | 3 | 68 | 55.35 | 1800 1992 | 50-62 | yes | 60 56 60 60 60 55 60 60 60 60 57 58 60 60 60 60 60 |
| p3x-2 | | 63 | 5 | 31 | 30 | 2 | 69 | 51.75 | 1800 1608 | 49-62 | yes | 56 60 60 60 60 59 60 60 60 60 52 53 60 60 60 60 60 |
| p3x-hpoff-1 | JIT_HUGEPAGES=off | 63 | 5 | 42 | 43 | 3 | 107 | 47.36 | 1800 1608 | 49-62 | part | 56 60 60 60 60 52 60 60 60 60 47 60 60 60 60 60 60 |
| p3x-hpoff-2 | JIT_HUGEPAGES=off | 63 | 5 | 26 | 22 | 3 | 51 | 52.19 | 1800 1992 | 50-62 | part | 55 60 60 60 60 59 60 60 60 60 52 60 60 60 60 60 60 |
| p3x-ring-1 | RENDER_2D_RING=shared | 63 | 4 | 27 | 27 | 2 | 67 | 54.92 | - | 50-62 | yes | 60 55 60 60 60 59 60 60 60 60 61 57 60 60 60 60 60 |
| p3x-ring-2 | RENDER_2D_RING=shared | 63 | 4 | 20 | 12 | 3 | 34 | 56.23 | - | 49-62 | yes | 56 60 60 60 60 58 60 60 60 60 57 60 60 60 60 60 60 |
| p3x-noexitpc-1 | MELONDS_JIT_NOEXITPC=1 | 63 | 5 | 37 | 36 | 4 | 103 | 51.37 | 1800 1992 | 50-62 | yes | 60 51 60 60 60 57 57 60 60 60 60 57 60 60 60 60 60 |
| p3x-noexitpc-2 | MELONDS_JIT_NOEXITPC=1 | 63 | 5 | 21 | 14 | 4 | 29 | 56.23 | 1800 1992 | 50-62 | yes | 56 60 60 60 60 58 60 60 60 60 59 60 60 60 60 60 60 |
| p3-alloff-1 | RENDER_GEOM_QUEUE=off RENDER_GEOM_SWITCH=off RENDER_GX_DMATIME=off JIT_HUGEPAGES=off JIT_CP15NOP=off JIT_REGION_TRIO=off JIT_FASTCLZ=off | 63 | 5 | 33 | 33 | 4 | 81 | 50.7 | 1800 1992 | 49-63 | part | 60 51 60 60 60 59 59 60 60 60 60 55 59 60 60 60 60 |

(`alloff` = all eight phase-3 toggles off in the phase 3 binary: `render.geom_queue`, `render.geom_switch`,
`render.gx_dmatime`, `render.geom_thread`, `jit.hugepages`, `jit.cp15nop`, `jit.region_trio`, `jit.fastclz` off, i.e.
phase 2's code paths in phase 3's code layout.)

| Toggle | Runs (frames lost / repeats) | Against `p3x` (27, 33, 31, 31 / 24, 26, 25, 30) | Verdict |
|---|---|---|---|
| `render.geom_thread` off (the worker) | 27, 33, 31, 31 / 24-30 vs on 61, 77, 69 / 69-91 | **on costs ~35 frames and ~50 repeats per race start, every run** | **hurts: off**. A fifth busy thread on four A55s (3 Gengis workers + the 2D thread + the emulator) |
| `jit.hugepages` off | 42, 26 / 43, 22 | within the spread | **nothing measurable in fps**. The premise holds: `[jit] code memory ... 32768 kB on huge pages` with it on, `0 kB` off, and the profile's iTLB misses go 5.24 -> 3.88 per 1000 instructions (section 3), L1I misses 11.08 -> 11.65 (noise). Keep on: it's free |
| `render.2d_ring=shared` (phase 2's) | 27, 20 / 27, 12 | within the spread (if anything, shared's two runs were the best of the day) | **nothing measurable**; the device can't tell split from shared in this scene. Keep whichever; no reason to flip |
| `MELONDS_JIT_NOEXITPC=1` | 37, 21 / 36, 14 | within the spread | **nothing measurable** |
| all eight phase-3 toggles off | 33 / 33 (one run) | within the spread | **phase 3's other seven changes together are not measurable on the panels**: the phase 2 -> phase 3 difference, worker off, is 38 -> 31 frames lost, inside the spread |

## 3. The profile of the emulator thread on the panels (the prompt's step 2)

`prof.hz=1000`, frames 1250-1850 of the run (the intro camera, countdown and start as the panel run times them),
phase 3 with the worker off, `MELONDS_JIT_PERFMAP=1`, names from the unstripped build. Full reports:
[prof-p3x.txt](prof-p3x.txt) (huge pages on) and [prof-p3x-hpoff.txt](prof-p3x-hpoff.txt) (off). A profiled run is
slower (a signal per sample: 54.4 fps over the window, 15 windows under 59), so its fps is not a benchmark number.

Header (huge pages on): 10,382 samples, 88% busy, **waiting 5.2% (`sched_yield` 5.1%)**, JIT code 29.3%; PMU per
frame: 31.4M cycles, 13.3M instructions, **IPC 0.42**, branch misses 14.3, L1I misses 11.7, L1D misses 4.1,
**iTLB misses 3.88** per 1000 instructions, 1934 MHz. Huge pages off: 30.4M cycles, 13.2M instructions, IPC 0.43,
branch 13.7, L1I 11.1, L1D 4.0, **iTLB 5.24** per 1000 instructions.

Top 20 (huge pages on):

| % | Symbol |
|---|---|
| 24.85 | JIT blocks |
| 8.97 | `GPU3D::Run()` (13.0% in the base's headless profile of 2026-10-18) |
| 6.15 | `Emu::run_frame()` (the frame loop with `NDS::RunFrame` inlined: the base's `main::{lambda()#1}` 8.0%) |
| 6.02 | `GPU3D::SubmitPolygonBodyT<true, false>()` (polygon setup; 5.3% as `SubmitPolygonBody<true>` in the base) |
| 5.09 | `__sched_yield` (the emulator thread spinning for the 2D thread or Gengis: `render.2d_fence_wait=spin`, the default; 0% headless) |
| 4.32 | `GPU3D::WriteGXFIFOBlock()` (the GX FIFO DMA's block path; the base's `WriteToGXFIFOBody` 6.4% + part of `DMA::Run9` 5.3%) |
| 3.27 | `GPU3D::SubmitVertexBody<true>()` |
| 3.18 | the SPU's event lambda |
| 1.93 | `GPU3D::CalculateLightingBody<true>()` |
| 1.81 | `DMA::Run9GX()` |
| 1.58 | `RunSamples<2>` (SPU, `core.fast_samples`) |
| 1.44 | JIT `stub_0000ed00` (DynExit) |
| 1.15 | JIT `stub_0000ec00` (ExitCheck) |
| 0.98 | `NDS::SliceChain9` (`jit.slice_chain`) |
| 0.79 | JIT `stub_0000eb00` (the patched memory calls) |
| 0.69 | `YSortCounting` (`render.geom_sort`) |
| 0.68 | `pthread_cond_signal` |
| 0.67 | `BananaRenderer::Submit()` |
| 0.65 | `GPU::StartScanline()` |
| 0.63 | libc+0x89eec |

By area: 3D geometry 27.8%, JIT code 24.9%, scheduler/RunFrame/events 8.4%, SPU 6.2%, memory/IO slow paths 6.2%,
waits and wake-ups 6.1%, JIT dispatch and exits 5.7%, 2D and video 4.4%, DMA 3.4%, JIT compiler 0.4%.

What's new against the base's profile: `GPU3D::Run` is down from 13.0% to 9.0% and the GX FIFO/DMA path from ~11.7%
to ~6.1% (the session's geometry work shows), the JIT's share is unchanged (~25% blocks + 5.7% stubs), and **5% of
the thread is now visible as spinning** (`__sched_yield`): on the panels the emulator thread waits for the 2D thread
and Gengis by yielding, which headless runs never showed. `render.2d_fence_wait=block` (session 2's tools branch,
one `--set` away) is the obvious next A/B on the panels; untested here. The hottest JIT block is still the word-copy
loop `mj9_0214d048` (0.35%); the top guest blocks are the ARM9 BIOS/ITCM handlers at 0x01ff8xxx.

## 4. Pacing (the prompt's step 3)

Covered by every run in sections 1 and 2 (the same `kms.stats=60` windows, repeats and drops, the cpugov log): phase 3
with the worker off repeats 24-30 refreshes a run against the base's 84-126, drops 2-6 either way (the drops are at
the game's start). No repeats outside the three dips named above. The governor's settled clock after the start is
1800 MHz for every build (section 1).

## 5. Exactness on the games the cloud didn't have (the prompt's step 4): NOT RUN

`exact.sh` and the frame/audio dumps are headless; the user ruled headless tests out for this session. So Golden Sun:
Dark Dawn and HeartGold are **unchecked on the device** for 0063 `render.gx_batch`, 0066 `render.gx_dmarun`, 0087
`jit.region_trio` and everything else. This is the biggest gap in this report. If the user allows it, the three
commands are ready on the Plus: `exact.sh hg-<tag> <bin> [--set ...]` (the kit's, HeartGold, 42 frames) and the same
with Golden Sun from the user's slot-1 field state (`/data/states/nds/Golden Sun - Dark Dawn.1.mst`, walk input in
`/data/bananads-dev/s3/gs-input.txt`), base vs phase 3 at its defaults vs phase 3 with every toggle off. ~40 s each.

What was seen on the panels: all 25 Mario Kart runs reached the race and ran it (62-63 windows each, the race's
windows at 60), the logs hold no errors or warnings beyond the expected ones, the game quit cleanly every time. That
is a play check of one game, not an exactness check.

## 6. Verdicts

| Phase | Verdict | Why |
|---|---|---|
| **Phase 1** (`mk-checkpoint1`, 8e98258) | **keep** | the race start's lost frames 95 -> 40 and the repeats 84-126 -> ~40 a run; the only phase with a gain the panels can see |
| **Phase 2** (`mk2`, 3a743f3) | **keep** | 40 -> 38 lost: nothing the panels can measure, nothing lost either; `render.2d_ring` split vs shared indistinguishable. Exactness on GS/HG unchecked here |
| **Phase 3** (`mk3`, ea310c8) | **keep with `render.geom_thread=off`, and flip its default to off** | the worker costs ~35 frames and ~50 repeats per race start on the Plus (four A55s, no free core); the other seven toggles together are not measurable (38 -> 31, inside the spread), none hurts, huge pages do cut iTLB misses by a quarter |

Against the brief's target (60 at 1992 MHz with headroom): not reached. Phase 3 with the worker off still loses
~30 frames per race start in three one-second dips at 1992 MHz, and the thread's IPC is 0.42 with 5% of it spinning.

## 7. For the next session

- Start from [prof-p3x.txt](prof-p3x.txt), a profile taken on the panels, not headless: the 5% `sched_yield` is
  real play's cost and is the first thing to A/B (`render.2d_fence_wait=block`).
- Don't add threads to the emulator's work on the Plus: the four cores are taken (3 Gengis + 2D + the emulator).
  A worker only pays if Gengis runs with 2 threads; that trade (`render.gengis_threads=2` + `render.geom_thread=on`)
  was not tested today.
- The remaining dips are short (one second each: the track load, the countdown's first second, the start). Per-second
  windows hide what happens inside them; a finer `kms.stats` (10-20 frames) on the panels would show whether they
  are a few long frames (loading, JIT compiling the race's code: ~4.5 blocks a frame) or steady slowness.
- Run the exactness compare on Golden Sun and HeartGold before merging phase 2 or 3 (section 5).
- Numbers from today compare only with each other (local GCC 14.3 builds, the panels, today's heat); not with the
  CI artifacts or yesterday's `kmsmk` runs of the 0.4.0 binary (which still show the same base picture: 46-105
  frames lost, see `/data/kmsmk/v040*` on the Plus).

Raw logs: `device-evidence/kms/<run>.log` (the run's header line, then the whole `bananads.log`), the scripts
`device-evidence/kmsmk4.sh`, `kmssum3.sh`, `kmstable.py` (the tables above are its output), `build-s3.sh` (the
builds), `device-evidence/headless-stopped/` (the five stopped headless runs).

## 8. Continued after the report (16:00-17:00 UTC): `bananads-perf/mk4`, both handhelds

The user's instruction: go ahead with the best setting, continue optimising. New SuperDrastic branch
**`bananads-perf/mk4`** (pushed) = `mk3` ea310c8 + `render.geom_thread` **off by default** (09bd3d7) +
**`render.2d_fence_wait=block` by default** (60d5e2f: session 2's tools commit 9d26e4d cherry-picked onto phase 2's
split job ring; every wait for the 2D thread blocks on a condition variable instead of spinning on `sched_yield`) +
`sched.emu_nice` / `sched.emu_cpu` / `sched.2d_nice` / `sched.gengis_nice` (99c4b81, A/B settings, off) + the merge
of `claude/bananads-audio-pitch` c489375 (ba8d479) + Gengis's render threads waiting for the frame's conversion on a
condition variable instead of spinning (the last commit). Local KMS builds as before (`k-mk4` = up to 99c4b81,
`k-mk4b` = with the audio merge and the Gengis wait).

### 8.1 Phase 3's worker with two Gengis threads, and the fence null test (the Plus, phase 3 binary, worker off unless said)

| run | env | windows | under 59 | frames lost | repeats | drops | short audio chunks | worst window fps | cpugov steps (MHz) | SoC °C | huge pages | windows 17-33 (fps) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| s4-ctl-1 | | 63 | 7 | 37 | 34 | 6 | 84 | 51.43 | 1800 1992 | 45-60 | yes | 60 51 60 60 60 59 60 60 60 60 60 56 57 59 60 55 57 |
| s4-ctl-2 | | 63 | 4 | 32 | 28 | 5 | 78 | 49.98 | 1800 1608 1800 | 49-62 | yes | 52 60 60 60 60 50 60 60 60 60 57 60 60 60 60 60 60 |
| s4-block-1 | RENDER_2D_FENCE_WAIT=block | 63 | 5 | 30 | 24 | 2 | 69 | 52.93 | - | 50-62 | yes | 60 61 56 60 60 60 58 60 60 60 60 59 60 60 60 60 60 |
| s4-block-2 | RENDER_2D_FENCE_WAIT=block | 63 | 5 | 33 | 27 | 4 | 71 | 52.18 | 1800 1992 | 49-63 | yes | 60 52 60 60 60 53 60 60 60 60 57 56 60 60 60 60 60 |
| s4-g2-1 | RENDER_GENGIS_THREADS=2 | 63 | 4 | 38 | 40 | 2 | 107 | 44.56 | 1800 1992 | 50-62 | yes | 57 60 60 60 60 51 60 60 60 60 45 60 60 60 60 60 60 |
| s4-g2-2 | RENDER_GENGIS_THREADS=2 | 63 | 4 | 23 | 21 | 3 | 43 | 53.79 | 1800 1992 | 50-63 | yes | 57 59 60 60 60 58 60 60 60 60 54 60 60 60 60 60 60 |
| s4-g2gt-1 | RENDER_GENGIS_THREADS=2 | 62 | 12 | 95 | 115 | 0 | 221 | 44.28 | 1800 1608 | 50-62 | yes | 60 55 60 60 60 57 48 44 48 58 55 54 58 47 60 60 60 |
| s4-g2gt-2 | RENDER_GENGIS_THREADS=2 | 62 | 8 | 76 | 87 | 3 | 183 | 44.75 | 1800 1608 | 50-62 | yes | 52 60 60 60 60 57 48 60 46 60 45 48 59 60 60 60 60 |

`s4-block`: `render.2d_fence_wait` does not exist in the phase 3 binary, so these two runs are a null test: 30 and
33 against the controls' 37 and 32 (the spread, again). `s4-g2gt` = the worker on with two Gengis threads: 95 and 76
frames lost, the worst of the day. The worker hurts even with a core free: it stays off.

### 8.2 `mk4` on the Plus: the blocking wait, nice values, a core pin, the shader off

| run | env | windows | under 59 | frames lost | repeats | drops | short audio chunks | worst window fps | cpugov steps (MHz) | SoC °C | huge pages | windows 17-33 (fps) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| m-ctl-1 |  | 64 | 3 | 17 | 11 | 4 | 28 | 57.14 | 1800 1992 | 49-62 | yes | 57 60 60 60 60 58 60 60 60 60 60 59 60 60 60 60 60 |
| m-ctl-2 |  | 64 | 3 | 16 | 11 | 4 | 32 | 56.24 | 1800 1992 | 49-63 | yes | 56 60 60 60 60 59 59 60 60 60 60 59 60 60 60 60 60 |
| m-spin-1 | RENDER_2D_FENCE_WAIT=spin | 63 | 3 | 21 | 18 | 4 | 46 | 51.43 | 1800 1992 | 50-62 | yes | 51 60 60 60 60 59 60 60 60 60 60 59 60 60 60 60 60 |
| m-spin-2 | RENDER_2D_FENCE_WAIT=spin | 63 | 4 | 28 | 20 | 5 | 55 | 52.16 | 1800 1992 | 49-63 | yes | 52 60 60 60 61 55 60 60 60 61 57 60 60 60 60 60 60 |
| m-nice-1 | SCHED_EMU_NICE=-10 | 64 | 3 | 16 | 9 | 2 | 29 | 56.17 | 1800 1992 | 50-63 | yes | 56 60 60 60 60 59 60 60 60 60 60 60 60 60 60 60 60 |
| m-nice-2 | SCHED_EMU_NICE=-10 | 63 | 5 | 21 | 15 | 3 | 35 | 56.23 | 1800 1608 | 49-62 | yes | 56 60 60 60 60 58 60 60 60 60 58 59 60 60 60 60 60 |
| m-nice2d-1 | SCHED_EMU_NICE=-10 SCHED_2D_NICE=-5 | 63 | 3 | 16 | 7 | 3 | 26 | 57.13 | 1800 1992 | 50-63 | yes | 57 60 60 60 60 59 60 60 60 60 60 60 60 60 60 60 60 |
| m-pin-1 | SCHED_EMU_CPU=3 | 63 | 3 | 16 | 8 | 2 | 24 | 57.54 | 1800 1992 | 49-63 | yes | 58 60 60 60 60 58 60 60 60 60 60 60 60 60 60 60 60 |
| m-pin-2 | SCHED_EMU_CPU=3 | 63 | 5 | 28 | 19 | 2 | 40 | 54.87 | 1800 1992 | 50-63 | yes | 56 60 60 60 60 58 60 59 60 60 55 57 60 60 60 60 60 |
| m-noshader-1 | KMS_SHADER=none | 63 | 3 | 15 | 8 | 3 | 22 | 57.14 | 1800 1992 | 50-58 | yes | 57 60 60 60 60 58 60 60 60 60 60 60 60 60 60 60 60 |
| m-noshader-2 | KMS_SHADER=none | 63 | 3 | 16 | 9 | 3 | 23 | 57.15 | 1800 1992 | 50-58 | yes | 57 60 60 60 60 58 60 60 60 60 60 60 60 60 60 60 60 |

- **`render.2d_fence_wait=block` (mk4's default) against `spin` on the same binary: 16-17 frames lost against 21-28,
  repeats 11 against 18-20.** With phase 3 (worker off) at 27-33 and the base at 78-110, the race start is now ~16
  frames, of which ~8 are the boot's first window and ~3 the track load (`m-stats15-1`, 15-frame windows: the only
  windows under 58 fps are the boot's two, one at 47 fps as the track loads, and three single-frame 58s).
- `sched.emu_nice=-10`, with `sched.2d_nice=-5`, `sched.emu_cpu=3`: 16-28, inside the spread. CFS doesn't need the
  help once nothing spins. The settings stay (off).
- `kms.shader=none`: 15-16: the LCD grid shader costs the race start nothing on the Plus (so the GPU isn't the
  limit here, unlike Golden Sun's house area in 0.4.0's notes).

### 8.3 The RG DS (non-Plus, 192.168.50.183, 640x480 panels, the card's `core.slice=auto` forced to 64 through the env, the card's per-game shader `lcd3x`)

| run | env | windows | under 59 | frames lost | repeats | drops | short audio chunks | worst window fps | cpugov steps (MHz) | SoC °C | huge pages | windows 17-33 (fps) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| r-base1 | | 62 | 6 | 59 | 60 | 17 | 177 | 41.85 | 1800 1992 | 33-47 | - | 57 60 60 60 53 57 50 60 60 59 42 60 60 60 60 60 60 |
| r-base2 | | 63 | 7 | 49 | 47 | 3 | 143 | 47.03 | 1800 1992 | 48-56 | - | 57 60 60 60 60 55 55 57 59 60 47 52 60 60 60 60 60 |
| r-p1-1 | | 63 | 6 | 39 | 35 | 7 | 128 | 50.02 | 1800 1608 | 44-50 | - | 52 60 60 60 60 57 60 60 57 60 50 60 60 57 60 60 60 |
| r-p1-2 | | 63 | 4 | 24 | 15 | 6 | 90 | 51.59 | 1800 1992 | 49-57 | - | 52 60 60 60 59 59 60 60 60 60 59 60 60 60 60 60 60 |
| r-p2-1 | | 63 | 9 | 51 | 49 | 7 | 175 | 52.97 | 1800 1992 | 47-53 | - | 60 57 60 60 60 53 60 59 60 60 59 54 60 55 60 60 60 |
| r-p2-2 | | 63 | 6 | 39 | 33 | 9 | 132 | 50.0 | 1800 | 49-57 | - | 60 50 60 60 60 57 60 60 60 60 58 55 60 60 60 60 60 |
| r-p3-1 | | 63 | 6 | 38 | 35 | 3 | 117 | 49.51 | 1800 1992 | 48-55 | yes | 52 60 60 60 60 56 59 60 60 60 50 57 60 60 60 60 60 |
| r-p3-2 | | 63 | 7 | 42 | 36 | 5 | 105 | 52.13 | 1800 1992 | 49-58 | yes | 57 60 60 60 60 55 59 60 60 52 53 55 60 60 60 60 60 |
| r-mk4-1 | | 63 | 2 | 17 | 7 | 5 | 73 | 56.2 | 1800 1992 | 49-55 | yes | 56 60 60 60 60 60 60 60 60 60 59 60 60 60 60 60 60 |
| r-mk4-2 | | 63 | 2 | 15 | 7 | 5 | 68 | 57.13 | 1800 1608 1800 1992 | 48-57 | yes | 57 60 60 60 60 59 60 60 60 60 59 60 60 60 60 60 60 |
| r-b-ctl-1 | | 63 | 2 | 15 | 5 | 7 | 56 | 57.13 | 1800 1992 | 45-56 | yes | 57 60 60 60 60 60 60 60 60 60 60 60 60 60 60 60 60 |
| r-b-ctl-2 | | 63 | 2 | 16 | 5 | 8 | 55 | 57.11 | 1800 1608 | 49-58 | yes | 57 60 60 60 60 60 60 60 60 60 60 60 60 60 60 60 60 |
| r-b-spin-1 | RENDER_2D_FENCE_WAIT=spin | 63 | 4 | 25 | 16 | 10 | 83 | 53.81 | 1800 1608 | 49-58 | yes | 57 60 60 60 60 57 60 60 60 60 60 60 60 60 60 60 60 |
| r-b-spin-2 | RENDER_2D_FENCE_WAIT=spin | 63 | 4 | 26 | 19 | 19 | 77 | 54.41 | 1800 1992 | 49-58 | yes | 57 60 60 60 60 54 60 60 60 60 54 60 60 60 60 60 60 |
| r-b-nice-1 | SCHED_EMU_NICE=-10 | 64 | 2 | 16 | 6 | 8 | 55 | 57.13 | 1800 1608 1800 | 49-57 | yes | 57 60 60 60 60 60 60 60 60 60 60 60 60 60 60 60 60 |
| r-b-nice-2 | SCHED_EMU_NICE=-10 | 63 | 2 | 15 | 5 | 7 | 55 | 57.14 | 1800 1608 | 50-58 | yes | 57 60 60 60 60 60 60 60 60 60 60 60 60 60 60 60 60 |
| r-b-pin-1 | SCHED_EMU_CPU=3 | 64 | 2 | 15 | 5 | 7 | 55 | 57.14 | 1800 | 50-57 | yes | 57 60 60 60 60 60 60 60 60 60 60 60 60 60 60 60 60 |
| r-b-pin-2 | SCHED_EMU_CPU=3 | 64 | 2 | 14 | 3 | 5 | 60 | 58.05 | 1800 1992 | 49-58 | yes | 58 60 60 60 60 60 60 60 60 60 60 60 60 60 60 60 60 |

The same picture: base 49-59 lost, phase 1 24-39, phase 2 39-51, phase 3 (worker on; this device has no shader and
more headroom) 38-42, **mk4 15-17 with 7 repeats**, mk4b 15-16 with 5 repeats; spin 25-26 against block's 15-16 on the
same binary. On the RG DS the only windows left under 59 are the boot's first and the track load (57 fps). Nice and
the pin: nothing (14-16).

### 8.4 Verdicts added
- **`render.geom_thread` off** (shipped as mk4's default), **`render.2d_fence_wait=block`** (mk4's default: the one
  change after phase 1 that the panels can see on both devices), the Gengis conversion wait blocking (exact by
  construction: a wait, not a computation; no separate A/B: it went in with the audio merge, `k-mk4b`'s runs are not
  worse than `k-mk4`'s).
- Nice values, a core pin, two Gengis threads, the shader: nothing on Mario Kart's race start; left off.

Raw logs: `device-evidence/kms/s4-*`, `m-*`, `device-evidence/kms-rgds/r-*`.

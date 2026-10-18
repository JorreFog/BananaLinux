# Session 2, agent `tools`: the defaults, and measuring on the device honestly

Branch **`claude/bananads-perf-tools`** on JorreFog/SuperDrastic, from `00c34c78e8de` (the commit `bananads.mk` pins).
Head: see the end of this file. Everything here is **untested on the device**; the numbers marked "device" are the
local agent's from 2026-10-18.

## The commits (in branch order)

| # | Commit | Toggle (default) | What |
|---|---|---|---|
| 1 | `c1b6cf2` BananaDS perf: render.2d_thread on by default at 1x too | `render.2d_thread` (`on`; was `on` only at 2x) | the default the guide asked for |
| 2 | `0428ed9` BananaDS perf: render.3d_share and render.3d_wait2d at 1x too | `render.3d_share`, `render.3d_wait2d` (`on`, now at 1x too) | the first session's perf/render `dc3f96a`, cherry-picked |
| 3 | `f62a109` BananaDS: a sampling profiler of the emulator's thread (prof.hz, off) | `prof.hz` (`0`) | a per-symbol profile without perf |
| 4 | `62c9728` BananaDS docs: the defaults audit | none | docs only: `core.fast_gxdma`/`core.fast_spu` superseded; `core.slice`'s row |
| 5 | `2d1c3e4` BananaDS tools: the device benchmarks prove they measure the scene | none (scripts; Gengis workers named `bds-gengis`) | hgquick.sh, threads.sh, hgprofile.sh, hgcommon.sh, walk.txt fixed |
| 6 | `12e39ae` CI: bananads-device | none | GCC 14.3 device build as a workflow artifact |
| 7 | `c27b7d9` BananaDS: prof.hz from a task-clock perf event, and the A55's counters (prof.pmu) | `prof.source` (`auto`), `prof.pmu` (`on` with prof.hz) | 4x the samples; `perf stat`'s counters |

## Expected device gains, ranked (with the reasoning)

1. **`render.2d_thread` on at 1x (commit 1): +21% fps at 1x** (HeartGold overworld, 1104 MHz). Not an estimate: the
   device measured exactly this setting on the base (+21%; GUIDE section 1). Only 1x changes: BananaLinux writes
   `render.scale = 2` into bananads.conf unless DraStic's `hires_3d` was 0, and at 2x it was already on. The
   launcher (`run-bananads`) doesn't set `render.2d_thread`; a card's own bananads.conf or bananads.env could.
2. **`render.3d_share`/`render.3d_wait2d` at 1x (commit 2): +1 to +4% at 1x, 0 at 2x.** With the 2D thread on at 1x,
   the emulator's thread still (a) copied each 3D line into the job (1 KB a line, 192 KB a frame: ~50 us of a
   13.4 ms frame, ~0.4%) and (b) waited in `GetLine` for each line's Gengis bin, or drew it itself
   (`render.gengis_help=1` once the frame is converted). Now the job points at Gengis's row and the 2D thread does the
   waiting; the emulator's thread waits for the 3D frame only at VBlank. The device measured 1x + 2D thread at +26%
   with the first session's whole series against +21% on the base; this commit is the emulator-thread part of that
   5-point difference (the rest was the 2D thread's own changes), so +1..4% is an upper-bounded guess. At 2x the code
   path is the base's (the matrix's 2x configurations are identical to the base).
3. **The profiler (commits 3, 7): 0% (off by default).** It is what the guide says work must start from. When on,
   each sample costs a signal (a few microseconds), so a profiled run's fps is not a benchmark number.
4. **The defaults audit (commit 4): no flips beyond 1 and 2.** See the table below; the two toggles the brief named
   (`core.fast_gxdma`, `core.fast_spu`) turned out to be dead code with today's defaults.

## The defaults audit (every off-by-default, exact setting or variable that could take work off the emulator's thread)

Sources: `docs/SETTINGS.md`, every `cfg.flag/num/str` in `src/`, every `getenv` in `patches/melonds/*.patch` and
`src/`. Everything else is on by default already (all `jit.*`, `core.fast_*` except two, `MELONDS_IOFAST` routes 0xF,
`MELONDS_MEMFAST` 0x7, `MELONDS_GEOMFAST`, `MELONDS_DMABLOCK`, `MELONDS_GXBLOCK`, `MELONDS_JIT_INVAL16/HOTSMC/HOTIDX`,
`render.vramtrack_skip`, `render.geom_fast`, `render.2d_fast`, `render.2d_lazy`).

| Toggle | Off by default because | On the emulator's thread | Exact? | Verdict / expected HG gain |
|---|---|---|---|---|
| `render.2d_thread` at 1x | was "on at 2x only" | the Banana 2D engine's lines (the whole 2D) | yes (exact-suite 17 ROMs + difftest + tests; base vs new on 5 render ROMs + reload + direct) | **flipped (commit 1)**: +21% at 1x (device) |
| `render.3d_share`/`3d_wait2d` at 1x | applied only at 2x | the 1 KB copy a 3D line, the per-line bin waits | yes (render matrix 1x t0-t3 + 2x-t2 vs base: 17/17 SAME; exact-suite off vs on: FAILS=0) | **added (commit 2)**: +1..4% at 1x |
| `core.fast_gxdma` (0004) | "only by the toggle rule" | nothing today | yes, but | **dead code**: `DMA::Run9`'s `core.fast_dma` branch (0013, on) already writes main RAM -> 0x04000400 straight to `GPU3D::Write32`, and 0020 does the ascending case in blocks; the FastGXDMA check is reached only with `core.fast_dma=off`. 0% |
| `core.fast_spu` (0004) | "only by the toggle rule" | nothing today | yes, but | **dead code**: `SPU::Mix` runs `MixChannelsFast` (`core.fast_mix`, 0013, on); the FastSPU loop runs only with `core.fast_mix=off`. 0% |
| `render.gengis_threads` | default 3; the device benchmarks use 2 | indirectly: 5 runnable threads (emulator, 2D, 3 workers) on 4 A55s now that the 2D thread is on at both scales; a worker preempting the emulator's core costs it | yes (matrix t0-t3 same pictures) | **device A/B** (2 vs 3, `threads.sh` both). 0 to +3%; flip to 2 if it wins. The shipped default (3) is not what hgquick measures (2) |
| `power.cpu_latency_us` | battery | the wake-up latency of the threads the emulator's thread waits for (2D thread at the frame's end, Gengis bins at VBlank) | yes (no emulation effect) | **device A/B, kms only** (the setting applies with `frontend=kms`): an earlier session measured `100` at +1.5% at 2x; with the 2D thread now on at 1x there are more cross-thread waits. Battery cost: measure the cpugov clock too |
| `render.gengis_help` (1) | `0` makes the emulator's thread wait instead of drawing a bin | bins it draws itself at VBlank/GetLine | yes (same pictures) | **low**: waiting is not cheaper than drawing when the emulator's thread is blocked anyway. Read HG's `[gengis] the emulator's thread drew N of M bins` line first; A/B `0` only if N/M is large. ±1% |
| `render.gengis_defer` | untested | no (Gengis workers) | not checked | not an emulator-thread lever |
| `core.slice` 256/512/`auto` | not exact | the per-slice cost | **no** | DEAD-ENDS (Golden Sun: Dark Dawn) |
| `MELONDS_JIT_LOOPREGS` | black screen in HG | JIT loops | **no** on the device | DEAD-ENDS |
| `render.threaded`, `MELONDS_IOSTATS`, `MELONDS_JIT_PERFMAP`, `core.slice_stats` | soft renderer only / diagnostics | | | n/a |

Told `memio` (fast_gxdma) and `loop` (fast_spu) at 19:12.

One thing the audit found that matters for reading the device's "emulator thread 99%": in the base, the emulator's
thread **waits for the 2D thread by spinning on `std::this_thread::yield()`** (`BananaRenderer::WaitFence`,
`Drain2D`, `LogReserve`) and for Gengis's conversion the same way (`gengis_renderer.cpp:279`). `/proc` counts that as
busy. Under qemu the profiler showed `__sched_yield` at 2-5% of the emulator's thread on dsstress L5 / geomtest. The
device profile (`hgprofile.sh`) will say how much of HeartGold's 99% is yield: if it's several %, the first session's
`render.2d_fence_wait=block` (perf/render `8677fad`, frame-exact on the device in the integrated series) is the next
cherry-pick, and "99%" partly means "waiting for the 2D thread".

## The device A/B commands, most promising first

The kit is `/data/bananads-dev` (`KIT=`), holding `hg.nds`, `hg.sav` and the scripts from `bananads/tools/device/`
(all four `.sh` files plus `walk-lr.txt`: `hgcommon.sh` is sourced by the others). First run of each scale prints
`HG_HASH_1X=...`/`HG_HASH_2X=...` and keeps `hg-frame1500-<scale>.ppm`: look at it once (the overworld), then put the
md5 in `hgcommon.sh` (or the environment). Until then the runs say `[scene UNCHECKED ...]`.

Binaries: the CI's `bananads-aarch64-<sha>` artifact (GCC 14.3 like Buildroot, KMS/ALSA off: headless only) holds
`bananads` (stripped as Buildroot does), `bananads.debug` (unstripped), `bananads-difftest`. For the base, run the
workflow by hand on a branch at `00c34c78e8de` plus the workflow file, or build both with the same compiler.

```sh
# 0. the profile everything else should start from (one run each, ~20 s at 1104 MHz; names from <bin>.debug)
hgprofile.sh ./bananads                                    # 1x (the default scale of the binary)
hgprofile.sh ./bananads --set render.scale=2               # 2x, the BananaOS default
#    paste $KIT/prof-*.txt back. Also tells: the JIT-code share, __sched_yield (waits), the IPC and L1I/iTLB misses
# 1. render.2d_thread at 1x: the new default against the old (expect +21%)
CLK=1104000 EXTRA="--set render.scale=1" hgquick.sh ./bananads ./bananads   # then:
CLK=1104000 EXTRA="--set render.scale=1 --set render.2d_thread=off" hgquick.sh ./bananads
# 2. render.3d_share at 1x (expect +1..4%; off = the old 1x path)
CLK=1104000 EXTRA="--set render.scale=1 --set render.3d_share=off" hgquick.sh ./bananads
# 3. the threads, at the shipped Gengis default (3) and the benchmarks' (2), both scales
threads.sh ./bananads --set render.gengis_threads=3
threads.sh ./bananads --set render.gengis_threads=2
threads.sh ./bananads --set render.scale=2 --set render.gengis_threads=3
CLK=1104000 EXTRA="--set render.scale=2 --set render.gengis_threads=3" hgquick.sh ./bananads   # vs 2 (hgquick's)
# 4. exactness of the new defaults (the kit's exact.sh): base vs branch at 1x, and the branch with them off
exact.sh base-1x <base-bin> --set render.scale=1
exact.sh tools-1x ./bananads --set render.scale=1
exact.sh tools-1x-off ./bananads --set render.scale=1 --set render.2d_thread=off --set render.3d_share=off
# 5. KMS only (real play, not headless): power.cpu_latency_us=100 via /data/banana/bananads.env
#    (BANANADS_POWER_CPU_LATENCY_US=100, BANANADS_KMS_STATS=600): fps, repeats/drops, the cpugov clock, battery
# 6. only if HG's log shows the emulator's thread drawing many Gengis bins ("[gengis] ... drew N of M bins"):
CLK=1104000 EXTRA="--set render.gengis_help=0" hgquick.sh ./bananads
```

Note: the scripts now set `rtc.fixed` (for the frame hash) and walk left and right (`walk-lr.txt`; `walk.txt` is
fixed too): their fps is not comparable 1:1 with numbers taken with the old hgquick (which walked right only). Take
a new baseline with the base binary first.

## The tools

- **`prof.hz`** (commits 3, 7; SETTINGS.md `prof.*`, PERF.md "Measuring"). `--set prof.hz=1000 --set prof.from=1500`:
  samples the emulator's thread (whichever thread calls `Emu::run_frame`: main in headless, `bds-emu` in kms) from
  a `task-clock` perf event of that thread (`F_SETOWN_EX` to the thread, `SIGPROF`), else a
  `CLOCK_THREAD_CPUTIME_ID` timer (tick-limited: 250 Hz on BananaOS's kernel, `CONFIG_HZ=250`); the handler stores
  the PC in a preallocated array. At the quit (or `prof.frames`): JIT-code share (anything outside a module's code),
  by module, top functions (`.symtab`, or `prof.syms=<unstripped twin>`: checked against the running binary's entry
  point and `.text`, warns if it's another build), libraries by `dladdr`, by C++ class, JIT blocks/stubs by name with
  `MELONDS_JIT_PERFMAP=1`, and `prof.pmu`'s line: cycles, instructions, IPC, branch/L1I/L1D/iTLB misses per frame and
  per 1000 instructions, the clock while running. `prof.raw=<file>` dumps every address for `addr2line` offline.
  **Stripped binaries**: Buildroot strips `/usr/bin/bananads` (no BR2_STRIP setting in the defconfigs, so the default
  `strip`): no `.symtab`, and the non-PIE binary exports almost nothing to `.dynsym`. Keep the unstripped one
  (Buildroot: `output/build/bananads-<version>/bananads/buildroot-build/bananads`; CI: `bananads.debug`) next to it;
  hgprofile.sh passes `<bin>.debug` automatically. Without it the report gives `bananads+0x<offset>` and says so.
  With LTO, `NDS::RunFrame`'s slice loop is inlined into its caller: headless shows it as `main::{lambda()#1}`.
- **`hgquick.sh`** (`CLK=`, default 1992000; `EXTRA=`), **`threads.sh <bin> [--set ...]`** (CPU % per thread over 8 s
  of the overworld at 1104 MHz, `/proc/<pid>/task/*/stat`, names: `bananads` = main = the emulator in headless,
  `bananads-2d`, `bds-gengis` (new name), `bds-romread`), **`hgprofile.sh <bin> [--set ...]`** (one profiled run,
  frames 1500-2700 at 1104 MHz, writes `$KIT/prof-<date>.txt`). All via `hgcommon.sh`: the walk's keys checked (the old
  `1500eft:90` fails loudly), the save copied as `<ROM name>.sav` and its `[emu] save ...` log line required, frame
  1500 dumped (before the measured window) and its md5 compared with `HG_HASH_1X/2X`; failure prints `INVALID` and
  exits 1; fps outside `[FPS_LO=35, FPS_HI=CLK/10000]` prints `SUSPECT`. The old 8-minute hgprofile.sh (an A/B list
  plus perf) is replaced by the one profiled run.
- **CI `bananads-device`** (`.github/workflows/bananads-device.yml`, `bananads/cmake/bootlin-a55.cmake`): on a push to
  `claude/bananads-perf-**` / `bananads-perf/**` touching `bananads/`, or by hand with extra `cmake -D` options (e.g.
  `-DBANANADS_ALIGN=32:16:16`): Bootlin 2025.08-1 (sha256-checked, cached), Buildroot's flags (compile flags checked
  identical to `build-a64-device.sh`'s), LTO, tests and a smoke run with `prof.hz` under qemu, artifact
  `bananads-aarch64-<sha>` with stripped + unstripped + difftest + `BUILD.txt` + `SHA256SUMS`, kept 30 days. No
  releases or tags. KMS/ALSA off (the toolchain's sysroot has no libdrm/alsa-lib), so not for play.
- **A static aarch64 `perf`**: not done. The profiler plus `prof.pmu` cover the per-symbol profile and `perf stat`'s
  counters for the emulator's thread; a static perf needs the kernel tree and static elfutils/libtraceevent builds
  with the Bootlin toolchain, which I couldn't finish and check reliably in this session. If the next BananaOS image
  can ship perf, `MELONDS_JIT_PERFMAP=1` already writes the JIT map perf reads.

## Exactness checks run (all under qemu-dev on build-a64-device.sh builds; evidence in `session2/tools-evidence/`)

- Commit 1: `exact-suite.sh` on the base build, `render.2d_thread=off` vs `on` (1x): 17 ROMs' pictures and audio,
  difftest main RAM on 9, bananads-tests: **FAILS=0** (`es-2dthread-1x.txt`). Base binary's default vs the new
  binary's default at 1x (abcheck, every 10th frame of 300 + audio): dsstress L5, L10, ds2dtest, membench, geomtest,
  L5 with a savestate reload at 150, ds2dtest with `headless.direct`: **all SAME**; the new binary with
  `render.2d_thread=off` vs the base default: SAME; the `[2d]` stats line confirms the thread runs by default only in
  the new binary (`ab-2dthread.txt`).
- Commit 2: render's `matrix.sh` base vs commit 2 for 1x-t3 (2D off), 1x-t2-2d (direct), 1x-t1, 1x-t0 and 2x-t2:
  **17/17 SAME** (`matrix-share1x.txt`); `exact-suite.sh` `render.3d_share=off` vs `on`: **FAILS=0**
  (`es-share1x.txt`).
- Commits 3/7: `exact-suite.sh` `prof.hz=0` vs `prof.hz=1000 prof.from=0`: **FAILS=0**, every B picture run's log
  holds a report (`es-prof.txt`, `es-prof2.txt` at the head); difftest `--every` with the profiler on the B side:
  SAME. Under qemu `perf_event_open` is ENOSYS, so the suite exercised the timer path; the task-clock path was tested
  natively on x86 (a 3:1 synthetic load: 1009 samples/s, 75.1/24.9%), not under the emulator. Profiles on
  dsstress L5 / geomtest under qemu, stripped binary with and without `prof.syms`, and a wrong `prof.syms` (warns):
  `prof-*.txt`.
- Commit 5: the scripts on a fake kit (a test ROM as hg.nds, qemu, a fake cpufreq folder): a good run, the hash
  printed when unset, a wrong hash (INVALID, exit 1), no save (FAIL), the old walk (FAIL), 2x, threads.sh,
  hgprofile.sh with a stripped binary + `.debug` (`scripts-test.txt`). The clock pinning/restoring ran only against
  the fake folder.

## Risks

- Commit 1 puts a second thread in every 1x game. The 2D thread draws display-capture frames on the emulator's
  thread (as before) and all checks are frame-exact, but at 1x the device has only measured HeartGold. Play-test
  a 1x game with heavy mid-frame 2D effects and one with display capture (the `[2d]` line counts capture lines).
- Commit 2 was measured on the device only inside the first session's series (on top of `render.3d_pipeline`, which
  this branch lacks). The code doesn't depend on it, and the matrix is exact, but its gain alone is unmeasured.
- The profiler's signal lands in the emulator's thread: `SA_RESTART`, errno saved, no allocation; anything that
  blocks in a non-restartable call (`nanosleep`, `poll` with a timeout) on that thread returns EINTR early. The
  headless loop has none; the kms emulator thread was not tested (no libdrm here). It's off by default.
- `HG_HASH_*` is per build output: a change that alters pictures (or a different `render.gengis_threads`? no: the
  matrix shows the same pictures) fails the check. `SCENE_CHECK=off` exists for intentional picture changes.
- The CI workflow hasn't run before this push; its first run is on this branch (see its result below).

## Next, if the device confirms

1. Paste `hgprofile.sh` reports (1x and 2x, 1104 MHz) into the next session: they decide the work (the guide's rule).
   Check the `__sched_yield` share; if large, cherry-pick `render.2d_fence_wait` (perf/render `8677fad`).
2. If gengis_threads=2 beats 3 at the shipped settings, flip the default (exact: same pictures).
3. Profile Golden Sun: Dark Dawn, MKDS and MPH with `hgprofile.sh`'s settings (`ROM=`, `SAV=`, a walk): the games
   that don't make 60 at 1104 MHz.

# sim: rgds-sim (the RG DS simulator), verification, and the combined measurement — FINAL (2026-10-18)

## 1. rgds-sim (in `rgds-sim/`, README.md there)

A Cortex-A55 / RK3568 timing model as a qemu 9.2 TCG plugin (`rgdssim.c`), with `run.sh` (one workload: per-thread
ms/frame at 1104 and 1992 MHz, buckets, L1I/L1D/TLB/branch events, top functions and JIT blocks), `ab.sh` (A/B of two
binaries or two setting sets, A and B in parallel), `regions.py` (one thread's L1D misses by buffer and function),
`calibrate.py` + `calib/device-calib.sh` (refit the costs to device numbers), `tools/exact.sh` (exactness check),
`tools/set2d.sh` (the fixed 2D benchmark set).

Model versions (say which one a number was taken with):
- **v1** (16:11): A64 instruction classes; static in-order dual-issue schedule per block; L1I/L1D 32 KB 4-way, shared
  L3 512 KB 16-way; L1 iTLB/dTLB + 1024-entry L2 TLB (2 MB entries for madvise(MADV_HUGEPAGE) ranges); gshare + BTB +
  return stack; a stream prefetcher; linear costs in `params.json` (L3 30 cycles, DRAM 120 ns, mispredict 8).
- **v2** (17:05, the default since): + the A55's L1D write streaming (whole-line store runs stop allocating). Source:
  TF-A's cortex_a55.h (CPUACTLR_EL1 bit 24, CPUECTLR_EL1 L1WSCTL); thresholds ASSUMED. Also per-region miss tables.
- **v3** (17:15, opt-in `SIMARGS=prfm=1`): + software prefetch (PRFM decoded, address read from the registers), timed on
  the thread's instruction count (a load before the fill lands pays the rest).

Deterministic in st mode (A = A: +0.00% on 1x, ~0.05% on st 2x); dev mode (threads) is ±3-4% per thread. Run time:
cpubench ~30 s, L5 st ~1-2 min, dev 2x ~1.5-2 min (one qemu per run; frames 100-300 from headless.window=1 marks).

Traps found and fixed: host ASLR made L1I/L1D differ ~8% between identical runs (run.sh uses `setarch -R` since
~16:10); the frame-mark and two-run (--diff) methods differ ~1% (compare within one method; ab.sh does).

## 2. Calibration: honest status

The e67e3ef kit's 25 device rows were reproduced (same tree 4536791, same CI compiler and flags, same ROMs and
settings). Only one row is single-threaded; v1's defaults predict it at +1.9% with nothing fitted. The 24 threaded
rows (melonDS's soft renderer on its own thread) can't pin the costs: their frame time depends on how the two
threads overlap. Best bounded fit 9% RMS in-sample, 19% leave-one-ROM-out, parameters at the edges of the plausible
ranges: not adopted (kept as `params-fit.json`, printed by ab.sh as a sensitivity line). The device's 1104/1992 MHz
ratios say the model underestimates the clock-independent share. **First device job:** `rgds-sim/calib/device-calib.sh`
(st settings, 1104 and 1992 MHz, perf stat if present), then `calibrate.py --combine main --write params.json`.
Evidence: evidence/sim/calibration-v1.txt.

## 3. Where the time goes (evidence/sim/where-the-time-goes-v1.txt, v2-2d-thread.txt)

- At 2x (BananaLinux's setting) the 2D thread (`bananads-2d`) is as busy as the emulator's thread on base (L5: 4.1-4.3
  vs 4.4 ms @1104, v2) and the busiest thread once `render.3d_pipeline` moves the Gengis bins off the emulator's thread
  (L5: 2D 4.0-4.2 vs main 2.8-3.2); on 2D-heavy ROMs it leads by far (ds2dtest 2x: 16.5 vs 9.7-10.0 ms).
- The 2D thread's L1D cost after write streaming is loads: Composite/CompositeHires reading Gengis's 3D rows (~6k
  misses/frame, nearly all DRAM), the Gengis bins it draws itself, and on ds2dtest DrawScanlineFrom's 79k L1 misses
  (L3 hits) on ~640 KB of VRAM flats. The 2x output rows (~4 MB of writes/frame) are write-streamed: no stall.
- Gengis worker threads: 33-35% of their cycles are L1D. JIT code: ~20% L1I on the stress ROMs. cpubench: JIT code 52%,
  scheduler/core 24%, 2D 11.5%.

## 4. Verification (LEDGER.md L1-L13; evidence/sim/verify-*.txt)

All with my own builds of the agents' commits (build-a64-device.sh = the device's toolchain and flags):
- L1 render.2d_comp_unroll: VERIFIED, membench -5.1%, ds2dtest -5.9% (st), exact (17 ROMs + difftest 6).
- L2 jit.hugepages: VERIFIED small (-0.45% cpubench, -0.61% bigcode; the model's L2 TLB catches nearly all).
- L3 jit.irqchain: VERIFIED -17.4% on irqmask (its stress ROM); ~0 on the standard ROMs.
- L4 GCC 13 PGO: superseded by L7.
- L5 render pipeline/2D thread hand-offs: VERIFIED at 2x (main -27%, the work moves to the Gengis workers).
- L6 HOT_OPT=-O3: VERIFIED as small; L1I eats it on 3D ROMs (+35% L1I misses); not worth +15% .text with L1 in.
- L7 device PGO: VERIFIED -4..-13% (st), exact; caveat: trained on the same ROMs; and in the combined tree the stale
  profile loses its L1I gain: retrain on the final tree.
- L10 render.2d_conv16 (jit): -1.5% membench 1x. L11 render.2d_prefetch16: -2.1% under v3 only (fill timing assumed).
- L8, L9, L12, L13: PENDING (device verdicts or not re-measured separately by sim; L12/L13 are inside combined #2).

## 5. Combined measurement (base build-dev vs everything)

perf/integrated @ 4de7beb (my worktree /home/user/perf/sim/v-int; = render's final dc50d19 + its PERF.md commit, jit's
0021-0024 commits, core's f503a7a build options; no conflicts), built like the device (build-a64-device.sh) with the
base-tree PGO profile (prof-dev), vs build-dev (the base). rgds-sim v2, the frame's slowest thread, ms/frame @1104:

| workload | base | integrated | change |
|---|---|---|---|
| ds2dtest 2x (dev) | 2D thread 16.49 | 2D thread 11.49 | -30.3% (@1992 -29.5%) |
| membench-win 2x (dev; render's windows-over-3D ROM) | 2D thread 7.88 | 2D thread 4.27 | -45.9% |
| dsstress-L5 2x (dev) | main 4.3-4.6 | 2D thread 3.96-4.22 | -2..-14% (dev noise ±4%) |
| membench 1x (st) | 4.66 | 4.22 | -9.6% |
| dsstress-L5 1x (st) | 6.86 | 6.71 | -2.3% |
| cpubench-thumb (st) | 5.48 | 5.46 | -0.5% |

At 2x the emulator's thread drops 30-40% (render.3d_pipeline moves the Gengis bins to the workers, +23..27% there):
the 2D thread is now the frame's limit at 2x in every workload. Not counted: render.2d_prefetch16 (a prefetch, v3).
The PGO profile is from the base tree: its 1x gain mostly vanished (L1I misses back up); retrain it on the final tree
(core's train.sh; core was doing it: /home/user/perf/core/prof-int). Exact: pictures + audio identical to the base on
17 ROMs at 1x and 5 at 2x (rgds-sim/tools/exact.sh), plus render's matrix subset (dev2x 10 ROMs, 1x-t0).
Evidence: evidence/sim/combined-int1.txt, combined-int2.txt.

## 6. Device test commands (for the local agent)

Per-thread CPU time is what to compare at 2x (the model's critical path is the slowest thread):
```sh
# base vs integrated build, HeartGold 2x, 1104 MHz pinned, headless; per-thread CPU from /proc after the run
EXTRA="--set render.scale=2" hgquick.sh /usr/bin/bananads-base
EXTRA="--set render.scale=2" hgquick.sh /usr/bin/bananads-integrated
# toggles of the integrated build, one at a time, off vs on (each must be exact: rtc.fixed + dump_every pictures match)
EXTRA="--set render.scale=2 --set render.3d_pipeline=off" hgquick.sh /usr/bin/bananads-integrated
EXTRA="--set render.scale=2 --set render.2d_prefetch=on" hgquick.sh /usr/bin/bananads-integrated   # off by default
EXTRA="--set jit.hugepages=off" hgquick.sh /usr/bin/bananads-integrated
# calibration of rgds-sim (first): sh rgds-sim/calib/device-calib.sh /usr/bin/bananads <test ROM dir>
# the A55 revision (write streaming is disabled by TF-A on r0p0 without L2): grep -i revision /proc/cpuinfo
```

## 7. Limitations to keep in mind

Absolute ms are not calibrated (one clean device point); trust A/B deltas within one workload and one model version.
No kernel time, no thread hand-off waits, no cross-core coherence, no DRAM bandwidth model; the stream prefetcher
and write-streaming thresholds are assumptions; layout-driven L1I changes (±30% between builds of the same code)
are real in the model but the device's layout differs (KMS frontend), so treat L1I swings from unrelated code as noise.

# sim: rgds-sim (the RG DS simulator) and the verification of the team's results (2026-10-18)

Draft, updated as the session goes; the final version is marked FINAL at the top.

## What I made: rgds-sim v1 (rgds-sim/, README.md there)

A Cortex-A55 / RK3568 timing model as a qemu 9.2 TCG plugin (`rgds-sim/rgdssim.c`) with `run.sh` (one workload, a
per-thread report: ms/frame at 1104 and 1992 MHz, buckets, L1I/L1D/TLB/branch events, top functions and JIT blocks)
and `ab.sh` (A/B of two binaries or two setting sets). Per thread: A64 instruction classes, a static in-order
dual-issue schedule per block, L1I/L1D 32 KB 4-way, shared L3 512 KB 16-way, L1 iTLB/dTLB + 1024-entry L2 TLB (2 MB
entries for madvise(MADV_HUGEPAGE) ranges), gshare + BTB + return stack. Costs are linear weights in `params.json`,
applied by `report.py`, so the model can be refitted to device numbers without re-running (`calibrate.py`).

Deterministic in st mode (A = A gives +0.00% since run.sh disables host ASLR; evidence/sim/determinism-v1.txt).
Run time: cpubench ~30 s, dsstress-L5 st ~2-3 min (one qemu, frames 100-300 via headless.window=1 frame marks).

## Calibration: honest status

See rgds-sim/README.md "Calibration" and evidence/sim/calibration-v1.txt. In short: the e67e3ef kit's device rows
were reproduced (same tree, same CI toolchain and flags, same ROMs and settings). Only one of the 25 rows is
single-threaded; v1's documented defaults predict it within +1.9% with nothing fitted. The other 24 are threaded
soft-3D runs whose two-thread overlap the model can't see: no parameter set fits them well with plausible values
(best bounded fit 9% RMS in-sample, 19% leave-one-ROM-out, parameters at the bounds). v1 stays at the defaults;
`params-fit.json` is kept as a "miss-heavy" sensitivity check (ab.sh prints both). The device's 1104/1992 MHz ratios
say v1 underestimates the clock-independent part (DRAM, L3 domain, kernel). **First thing to do with the handheld:**
`rgds-sim/calib/device-calib.sh` (st settings, two clocks, perf stat if present), then `calibrate.py --combine main`.

## Verification (LEDGER.md)

(see LEDGER.md for each row; evidence/sim/verify-L*.txt)

## Device test commands

(to be filled)

## Dead ends and traps

- Host ASLR made L1I/L1D vary ~8% between identical qemu runs (guest mmaps land at random host addresses, so cache
  sets differ): fixed in run.sh with `setarch -R` at ~16:10; anything measured before that is noise at that level.
- The frame-mark method (one run) and the --diff method (two runs) differ by ~1% on the same binary: compare only
  within one method (ab.sh does).

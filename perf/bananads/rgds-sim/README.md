# rgds-sim: the RG DS (Cortex-A55 / RK3568) simulator for BananaDS

A timing model of the RG DS's CPU side, run under qemu-aarch64 (linux-user) as a TCG plugin. It runs BananaDS's real
aarch64 binary on a PC and reports, per emulated frame and per thread, the modeled A55 time at 1104 and 1992 MHz,
where it goes (JIT code, JIT runtime, scheduler, memory paths, 2D, 3D geometry, Gengis, audio, libc), and the
events behind it (L1I/L1D/L3 misses, TLB, branch mispredicts). It is deterministic in the single-threaded mode, so
an A/B difference of 0.1% is real (as a model difference, not necessarily on the device).

**What it is good for:** comparing two builds or two settings (A/B) on the same workload; finding where the A55's
time goes; catching changes that cut instructions but add cache/TLB misses (PERF.md's JIT change: -1% instructions,
+22% L1I, +43% iTLB, slower). **What it is not:** a cycle-accurate A55. See "Limitations".

## Files

| File | What |
|---|---|
| `rgdssim.c` | the qemu plugin (event counting, caches, TLBs, branch predictors, static per-block schedule) |
| `report.py` | per-frame report from two runs (N1 and N2 frames): applies the cost model (`params.json`) |
| `run.sh` | one workload: `run.sh <bin> <rom> <label> [--mode st\|dev\|raw] [--frames N1:N2] [--set k=v ...]` |
| `ab.sh`, `ab.py` | A/B: `ab.sh <binA> <binB> <rom> [--a "--set ..."] [--b "--set ..."] [--mode ...]` |
| `params.json` | the cost model's parameters (calibrated; its `version` names the model version) |
| `calibrate.py` | fits the free parameters to device frame times (`calib/`) |
| `calib/` | the device numbers used, the calibration runs' script and the fit's output |

## Running it

Needs a qemu-aarch64 with plugins (`--enable-plugins`, qemu 9.2; Ubuntu's has none), its `include/qemu` to build the
plugin (run.sh builds it into `build/` on first use), glib, python3 (+ numpy for calibrate.py), and an aarch64
sysroot for the dynamic BananaDS (`SYSROOT`, default `/usr/aarch64-linux-gnu`).

```sh
# cloud machine paths are the defaults; elsewhere set QEMU=<qemu-aarch64> QEMU_INC=<qemu-src>/include/qemu
OUT=/tmp/rgds rgds-sim/run.sh build/bananads roms/cpubench-cpubench-thumb.nds thumb          # st mode
OUT=/tmp/rgds rgds-sim/run.sh build/bananads roms/dsstress-L5.nds l5dev --mode dev            # BananaLinux's settings
OUT=/tmp/rgds rgds-sim/ab.sh build/bananads build/bananads roms/dsstress-L5.nds --a "--set core.slice=64" --b "--set core.slice=256"
OUT=/tmp/rgds rgds-sim/ab.sh base/bananads mine/bananads roms/cpubench-cpubench-thumb.nds --label mychange
```

- `--mode st` (default): `jit=on render.threaded=off render.2d_thread=off render.gengis_threads=0` (the build's default
  renderer, Gengis): one busy thread, exactly reproducible. Use it for A/B of anything on the emulator's thread.
- `--mode dev`: BananaLinux's settings, `render.scale=2 render.renderer=gengis render.gengis_threads=2` (2D thread and
  `core.slice` at their defaults, i.e. on/auto at 2x): every thread is reported (main = the emulator's thread; others
  by name or `threadN` in creation order). Thread interleaving makes the shared-L3 numbers vary a little run to run.
- `--mode raw`: only the common settings (as device-test.sh runs BananaDS); used for calibration.
- Always: `--frontend headless`, `rtc.fixed=2026-10-11_12:00:00`, `wifi.dns=off`, resume off, saves in the run dir,
  `MELONDS_JIT_PERFMAP=1` (JIT blocks named in the report). Your `--set`s come last and win.
- Frames: the report is (run of N2 frames - run of N1 frames) / (N2 - N1); default frames 100-300. The two runs go in
  parallel (2 qemu processes).
- Plugin arguments (structure, not costs) via `SIMARGS="l3=1048576 pf=0 ..."`; cost parameters via `PARAMS=<json>`.

Run time on this machine (4 shared cores, two qemu in parallel): cpubench-thumb ~40 s, dsstress-L5 (frames 60-180,
two threads) ~2.5 min, frames 100-300 about 1.5x that.

## The model

The plugin counts events per thread and per code location; `report.py` turns them into time with a linear cost
model, so the costs can be refitted to device numbers without re-running anything:

```
core cycles = base_scale * base
            + l3_cyc * (L1I misses + L1D load misses + st_cost * L1D store misses)
            + tlb_l2_cyc * (iTLB + dTLB misses that hit the L2 TLB) + walk_cyc * (page walks)
            + mispredict_cyc * (conditional mispredicts + indirect/return mispredicts)
time(f)     = core cycles / f  +  dram_ns * (L3 misses from fetch + loads + st_cost * stores)
```

The DRAM term is in nanoseconds (LPDDR4 at a fixed 1056 MHz), so a memory-bound workload gains less from the clock:
the model's 1104 -> 1992 MHz ratio is checked against the device's.

- **base**: each translation block's cycles from a static in-order dual-issue schedule: up to 2 instructions a cycle,
  at most one load/store, one multiply, one branch, FP/NEON only beside a non-FP instruction, divide and system
  instructions alone; a consumer waits for its producer (ALU 1, shifted-register logical 2, load 3, MUL 3, 64-bit MUL 4,
  DIV 8, FP/NEON 4 cycles); L1 hits assumed. Dependencies across blocks are not seen.
- **instruction classes** (per thread): A64 decode into alu, mul, div, ld, st, ldp, stp, SIMD/FP ld/st, atomics and
  exclusives, b.cond/cbz/tbz, b, bl, br, blr, ret, FP/NEON, system (mrs/msr/barriers/svc).
- **caches**: L1I and L1D per thread (each busy thread is assumed to have its own core: 4 A55s), shared L3; LRU,
  write-allocate, no write-back traffic modeled. L1D has a stream prefetcher (a miss next to one of the last 8 misses
  fetches the next `pfdeg` lines); L1I a next-line prefetch on a miss.
- **TLBs**: fully associative L1 iTLB and dTLB, a set-associative unified L2 TLB, 4 KB pages; ranges the guest
  `madvise(MADV_HUGEPAGE)`s get 2 MB entries (`thp=1`; BananaLinux's kernel has THP=madvise; qemu-user ignores it).
- **branches**: gshare (2-bit counters, `2^bpbits` entries, `bphist` bits of global history) for b.cond/cbz/tbz;
  a `btb`-entry direct-mapped BTB (last target) for br/blr; a `ras`-entry return stack for ret. The outcome is the next
  block actually executed (all A64 branches end a qemu translation block).

### Parameters and where the defaults come from

| Parameter | Default | Source |
|---|---|---|
| L1I, L1D | 32 KB, 4-way, 64 B lines | RK3568 datasheet (32 KB + 32 KB per core); 4-way / 64 B: Arm Cortex-A55 TRM (L1 caches) |
| L2 (private) | none | RK3568: 4x A55 with a 512 KB L3 in the DSU, no per-core L2 (datasheet; **verify**) |
| L3 | 512 KB shared, 16-way | size: RK3568 datasheet; **16-way ASSUMED** (DSU L3 configurations) |
| L1 iTLB / dTLB | 15 / 16 entries, fully assoc. | A55 TRM as recalled (**unverified**) |
| L2 TLB | 1024 entries, 4-way | A55 TRM (1024-entry unified L2 TLB; 4-way **as recalled**) |
| page | 4 KB (2 MB in THP ranges) | BananaLinux kernel |
| predictor | gshare 4096 x 2-bit, 10-bit history; BTB 256; RAS 8 | **ASSUMED** (Arm publishes no A55 predictor details) |
| latencies in the static schedule | load 3, MUL 3/4, DIV 8, FP 4 | **ASSUMED** after Arm's A55 Software Optimization Guide (approximate) |
| pairing rules | see above | **ASSUMED** (simplified from the A55 SWOG) |
| prefetchers | L1D stream, degree 2; L1I next line | **ASSUMED** (the A55 has a data prefetcher; its policy isn't modeled) |
| base_scale, l3_cyc, dram_ns, mispredict_cyc | see params.json | **fitted** to device numbers (below) |
| tlb_l2_cyc, walk_cyc, st_cost | 2, 25, 0.2 | **ASSUMED**, fixed during the fit |

## Calibration

(see below: filled in by the calibration run)

## Limitations (read before trusting a number)

- In-order issue is modeled per translation block; stalls across blocks, the store buffer, write-backs, DRAM
  bandwidth, bank conflicts, the A55's real prefetcher and predictor are not. Absolute ms are calibrated on a few
  workloads; A/B differences are what to trust, as model differences.
- Kernel time (syscalls, futex wakes, page faults, scheduling) is not modeled: qemu-user runs syscalls on the host.
  Thread hand-offs (a thread waiting for another) are not modeled: each thread's time is its own work. A change that
  moves work between threads shows as such; how the device overlaps them doesn't.
- Shared libraries (libc's memcpy/memset...) show as `libc/lib` addresses; the sysroot's glibc (2.39 here) is not the
  device's (2.41).
- `--mode dev` runs threads concurrently on the host: the shared L3's interleaving differs run to run (small).
- The JIT's code is attributed per qemu block; blocks without a perf-map entry (exit stubs) show as `stub/unknown`.

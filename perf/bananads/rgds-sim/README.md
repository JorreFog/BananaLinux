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
| `regions.py` | one thread's L1D misses per frame by buffer (address range) and function (v2+ runs) |
| `tools/exact.sh` | the verifier's exactness check (pictures every 50 frames + audio dump; difftest for one binary) |
| `tools/set2d.sh` | the fixed 2D benchmark set (dev 2x, st 1x, st 2x) |
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
- Frames: default 100-300. st/dev: one qemu; BananaDS's `headless.window=1` logs a line after every frame, the plugin
  reads those writes, runs plain qemu until frame N1-10, then counts and snapshots after frames N1 and N2 (builds
  from 00c34c7 on). `--diff` (default for raw, and needed for older builds such as e67e3ef) runs N1 and N2 frames as
  two parallel qemu processes and subtracts. ab.sh runs A and B in parallel (one qemu each).
- Plugin arguments (structure, not costs) via `SIMARGS="l3=1048576 pf=0 ..."`; cost parameters via `PARAMS=<json>`.

Run time on this machine (4 shared cores, loaded): cpubench-thumb ~30 s (st, frames 100-300), dsstress-L5 st ~2-3 min,
dev mode at 2x ~3-5 min; --diff runs ~1.5-2x that. An A/B is one run's time (A and B in parallel).

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

### Model versions

- **v1** (16:11): as described above.
- **v2** (17:05): + L1D **write streaming**: after `ws`=4 cache lines written whole in a row (by up to `wsn`=2 interleaved
  streams), store misses stop allocating in L1D (in the L3 too after `wsl3`=128 lines) and cost nothing but bandwidth,
  until a line is left partly written. Source for the mechanism: Arm TF-A `include/lib/cpus/aarch64/cortex_a55.h`
  (CPUACTLR_EL1 bit 24 DISABLE_WRITE_STREAMING; CPUECTLR_EL1 bits 25-26 L1WSCTL, the L1 threshold); TF-A's workaround for
  erratum 778703 turns it off on r0p0 parts without L2 (check the RK3568's revision: /proc/cpuinfo). **ASSUMED**: the
  thresholds (the A53's equivalent fields default to 4 lines for L1, as I recall the A53 TRM), the stream count, the L3
  rule. Effect: the 2D thread's output rows at 2x (~4 MB of writes a frame) no longer cost L1D stalls. Also writes
  `f<N>.regions` (L1D misses per thread x function x 64 KB region): `regions.py <run dir> <thread>` lists them by buffer.
- **v3** (17:12, opt-in: `SIMARGS=prfm=1`; v2 stays the default): + **software prefetch**: PRFM/PRFUM (PLD/PST; L1 or
  L2/L3 targets) are decoded and their address read from the registers at run time (qemu runs them as no-ops). The fill
  is timed on the thread's own instruction count: it lands `pfl3`=24 (L3) or `pfdram`=120 (DRAM) instructions later
  (~30 / ~150 cycles at ~0.8 IPC, ASSUMED); a load that reaches the line earlier pays the remaining fraction of the miss
  (`swpf_late_*`). Check on a synthetic scan (stride 64 B, 32 MB): prefetch 4 lines ahead leaves 80% of the DRAM
  latency, 16 lines 33%, 64 lines 0%.
- Choose a version with SIMARGS: v1 = `ws=0`, v2 = default, v3 = `prfm=1`. ab.sh compares within one version.

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

## Calibration (model v1, 2026-10-18): what the device numbers can and can't pin down

**Data.** The only device numbers with a ROM we have are the e67e3ef kit's (BananaDS docs/results-B-rgds-20261011.md):
stress L1/L5/L10 and L5 with a busy ARM7, interpreter / melonDS JIT / BananaJIT, 1104 and 1992 MHz (24 rows), and
one row with `render.threaded=off` (BananaJIT, L10, 1992 MHz). e67e3ef is the tree of 4536791 (CI's merge of PR #9 into
an unmoved main); I built it as its CI did (`.github/workflows/bananads.yml`: ubuntu-24.04, Ubuntu's
g++-aarch64-linux-gnu 13.3, `package-device.sh`: CMake Release = -O3, no LTO, no -mcpu, pristine melonDS; SDL/ALSA off
here, the headless path is the same), and the kit's stress ROMs from 4536791's `stressrom/` (arm L1/L5/L10 are
byte-identical to /home/user/perf/roms; the arm7-thumb one differs and was rebuilt). Each row was run with the kit's
settings (`--mode raw`, frames 60-180): `calib/device-e67e3ef.tsv`, runs listed in the evidence file.

**Result.** `params.json` (v1) stays at the documented/assumed defaults: base_scale 1.0, L3 30 cycles, DRAM 120 ns,
mispredict 8 cycles, L2-TLB hit 2, walk 25, store-miss weight 0.2.
- The one **single-thread** device row (BananaJIT, L10, `render.threaded=off`, 1992 MHz): device 19.16 ms/frame,
  model 19.53 ms (**+1.9%**), with no parameter fitted.
- The 24 rows ran melonDS's **threaded** soft renderer (the default then): frame time depends on how the emulator's
  thread and the render thread overlap (scanline semaphores, the VCount-144 wait, frames that alternate heavy/light at
  L10), which a per-thread model does not see. With v1's costs and only the overlap (two fractions) and a per-frame
  constant fitted: **RMS error 18.6%, max 32.7%** (L1 too slow by 16-33%, L10 too fast by 14-31%).
- Fitting the cost parameters too (bounded to plausible ranges: base_scale 0.5-1.6, L3 5-80 cycles + 0-60 ns, DRAM
  60-250 ns, mispredict 4-16): **RMS 9.0%, max 20.8% in-sample; leave-one-ROM-out RMS 19.0% (max 48.6%); fit at
  1104 MHz only -> 1992 MHz RMS 18.3%**. The fitted values sit at the edges of the ranges (base_scale 0.5, L3 80
  cycles + 23 ns, DRAM 60 ns, mispredict 16) and the single-thread row gets worse (+7 to +21%): the threaded rows push
  the costs from instructions towards misses because that is what separates L5 from L1 there, not because the A55 is
  like that. Unbounded fits are worse still (base_scale -> 0). Written as `params-fit.json` (v1-fit-threaded) for a
  sensitivity check only: `PARAMS=rgds-sim/params-fit.json`. A conclusion that holds under both is safer.
- The device's T(1104)/T(1992) ratios (1.47-1.65 against 1.80 for pure core time) say 20-42% of each frame at
  1992 MHz doesn't scale with the clock; v1's ratios are 1.64-1.73 for the same runs: **v1 underestimates the
  clock-independent part** (DRAM, L3 if it is in its own clock domain, kernel/thread hand-offs), so its 1992 MHz
  numbers are optimistic and memory-bound changes are undervalued against instruction cuts.

So: **v1 is not calibrated in the strict sense** (one clean data point); the reason is concrete (all other device
rows are two-thread runs whose overlap the model can't see), and the fix is a device run in rgds-sim's own st
settings, below. Evidence: `evidence/sim/calibration-v1.txt` (commands, every row, every fit variant).

### Re-calibrating with the handheld (the local agent)

1. On the RG DS: `sh rgds-sim/calib/device-calib.sh /usr/bin/bananads <rom dir>` (as root, nothing else running).
   It pins 1104 and 1992 MHz and runs cpubench thumb/arm/thumb-arm7, membench, ds2dtest, sndbench and stress
   L1/L5/L10 in the st settings (one busy thread), headless, frames 300-1500; with `perf` present it records cycles,
   instructions, L1I/L1D misses and branch misses per run too. Output: `st-<rom> <MHz> <fps>` lines.
2. Here (or on any PC with this qemu): run the **same binary** (copy it off the device, or build the same commit with
   tools/build-a64-device.sh) on the same ROMs: `OUT=<dir> rgds-sim/run.sh <bin> <rom> st-<rom> --mode st`.
3. `python3 rgds-sim/calibrate.py <device output> <dir> --combine main --write rgds-sim/params.json --version v2-device`
   (all four costs free; the leave-one-ROM-out and 1104 -> 1992 errors are printed). With `perf stat` numbers, compare
   the model's L1I/L1D/branch-miss counts per frame with the device's directly (they are per frame in report.json).
4. Version the result (`version` in params.json) and say which version every number was taken with.

## Limitations (read before trusting a number)

- In-order issue is modeled per translation block; stalls across blocks, the store buffer, write-backs, DRAM
  bandwidth, bank conflicts, the A55's real prefetcher and predictor are not. Absolute ms are calibrated on a few
  workloads; A/B differences are what to trust, as model differences.
- Kernel time (syscalls, futex wakes, page faults, scheduling) is not modeled: qemu-user runs syscalls on the host.
  Thread hand-offs (a thread waiting for another) are not modeled: each thread's time is its own work. A change that
  moves work between threads shows as such; how the device overlaps them doesn't.
- Shared libraries (libc's memcpy/memset...) show as `libc/lib` addresses; the sysroot's glibc (2.39 here) is not the
  device's (2.41).
- `--mode dev` runs threads concurrently on the host: thread timing changes the work split (who draws which Gengis
  bin) and the shared L3's contents: ±3-4% per thread run to run. For 2D-engine A/Bs at 2x use `--mode st --set
  render.scale=2` (deterministic, ~0.05%) and dev mode only for the thread split.
- No cross-core coherence: a line another thread writes stays valid in this thread's L1 (producer/consumer buffers
  smaller than the L1 are under-counted). No DRAM bandwidth limit, no write-back traffic cost.
- L1I misses move with the link layout (±30% between builds of the same hot code, render's 17:32 post): the device's
  layout differs (KMS frontend), so an L1I swing from unrelated code is noise, not a result.
- The JIT's code is attributed per qemu block; blocks without a perf-map entry (exit stubs) show as `stub/unknown`.

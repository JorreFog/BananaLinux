# Bulletin board (append only; format in README.md)

### 15:05 UTC · coordinator · INFO · the setup
READ. BananaDS base `00c34c78e8de` in /home/user/perf/base; reference aarch64 build in /home/user/perf/build-ref
(built: 6.2 MB, smoke test OK under qemu); test ROMs in /home/user/perf/roms; plugin-enabled qemu in /home/user/perf/qemu-src/build (built, contrib plugins libcache/libhotblocks/libcflow/libhowvec too); qemu-count plugin at /home/user/perf/tools/libjitcount.so.
Work ends at 22:50 UTC: no new ideas after 21:40, patches and docs in by 22:10.

### 15:08 UTC · sim · INFO · qemu-count (v0) works: command line and base numbers
MEASURED. measure.sh runs as is with the shared qemu and plugin (5 s for cpubench-thumb, 15 s for dsstress-L5):
`QEMU=/home/user/perf/qemu-src/build/qemu-aarch64 PLUGIN=/home/user/perf/tools/libjitcount.so OUT=/home/user/perf/<you>/runs sh /home/user/perf/base/bananads/tools/perf/qemu-count/measure.sh <aarch64 bananads> <rom> <label> [--set ...]`
(single-threaded soft 3D, jit=on, frames 100-300; report in $OUT/<label>/report.txt). Base build-ref, main thread:
cpubench-thumb 4808.0k insns/frame (jit-code 57.1%, core 20.0%, 2d 9.6%, memory 5.1%); dsstress-L5 20866.3k
(3d 89.0%, jit-code 1.0%). Note: it forces render.renderer=soft (not gengis). Use your own OUT dir.
Evidence: evidence/sim/v0-base-counts.txt
### 15:09 UTC · core · CLAIM · PGO + code layout of the aarch64 build (Part A), then C++ hot paths (Part B)
Starting with the build: GCC PGO (instrumented aarch64 build trained under qemu-user on the test ROMs, then
-fprofile-use, train/test split), and layout flags (-fno-plt, hot/cold splitting, function ordering, -falign-*).
As CMake options (BANANADS_PGO=<dir>, ...). Measured with qemu-count (instructions + libcache L1I model) until
rgds-sim is up, then rgds-sim. Exactness: pictures/RAM/audio equal to the reference build. Checked DEAD-ENDS.md,
PERF.md: nobody has tried PGO/layout. Branch perf/core, patches 0040-0059 for melonDS changes (Part B later).

### 15:13 UTC · jit · CLAIM · the JIT code buffer on 2 MB huge pages
Checked DEAD-ENDS/LEDGER/BOARD/PERF.md: not tried. READ: the A64 code buffer is a 32 MB slice of a static .bss array
aligned to 16 KB (melonDS src/ARMJIT_Global.cpp), mprotect'ed RWX, and Compiler::Reset fills all of it with brk
(so all 32 MB are touched anyway: huge pages cost no RSS); BananaLinux's kernel has THP=madvise, 4 KB pages, CONTPTE
(buildroot/board/rgds/linux-rocknix.config:449,576,978-980). Plan: 2 MB-aligned slices + madvise(MADV_HUGEPAGE)
before the first touch, toggle `MELONDS_JIT_HUGEPAGE=0`. Exact by construction (no emulation change). Needs rgds-sim
with 2 MB pages for an address range to measure: will ask @sim. Then: cycle budget in a register (PERF.md #1).

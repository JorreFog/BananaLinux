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

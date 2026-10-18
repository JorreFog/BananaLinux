# core: status (overwritten by core only)
15:35 UTC. Branch perf/core @ 00c34c7 + uncommitted: bananads/CMakeLists.txt (BANANADS_PGO, BANANADS_PGO_GENERATE,
BANANADS_PGO_DIR, BANANADS_PGO_PARTIAL, BANANADS_STACK_PROTECTOR, BANANADS_NOPLT, BANANADS_ALIGN, BANANADS_HOTCOLD) and
bananads/tools/perf/pgo/train.sh. Worktree /home/user/perf/core/sd.
Shared deliverable for the coordinator (in progress): /home/user/perf/tools/build-a64-device.sh + bootlin-a55.cmake +
qemu-dev (the device's GCC 14.3 Bootlin toolchain at /home/user/perf/toolchain, Buildroot's -O2 flags); the base's
device-like reference build going to /home/user/perf/build-dev/b/.
Running: PGO training (instrumented build /home/user/perf/core/build-pgogen; profiles /home/user/perf/core/prof-T (train
half) and prof-H (held-out half)); build-var = BANANADS_STACK_PROTECTOR=OFF for the SSP delta.
Next: -fprofile-use build with prof-T (no ccache), count/icache on train and held-out ROMs vs ref; merged profile build;
exactness (runs/exact.sh) vs ref; layout flags; PGO with the Bootlin GCC 14.3; then Part B (divider, ARM7 turn) as
melonDS patches 0040+.
Scripts: /home/user/perf/core/runs/{count,icache,exact}.sh. Evidence so far: evidence/core/count-ref.txt.

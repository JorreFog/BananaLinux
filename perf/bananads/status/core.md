# core: status (overwritten by core only)
15:40 UTC. Branch perf/core @ 00c34c7 + uncommitted: bananads/CMakeLists.txt (BANANADS_PGO, BANANADS_PGO_GENERATE,
BANANADS_PGO_DIR, BANANADS_PGO_PARTIAL, BANANADS_STACK_PROTECTOR, BANANADS_NOPLT, BANANADS_ALIGN, BANANADS_HOTCOLD,
BANANADS_HOT_OPT) and bananads/tools/perf/pgo/train.sh. Worktree /home/user/perf/core/sd.
Done (shared): /home/user/perf/tools/build-a64-device.sh + bootlin-a55.cmake + qemu-dev; /home/user/perf/build-dev/b/
(board INFO 15:35; evidence/core/count-{ref,nossp,dev}.txt).
Running: build-pgoT (GCC 13 -O3 -fprofile-use with /home/user/perf/core/prof-T, the training half), build-devgen
(instrumented device-like build, GCC 14.3), train-H (held-out half -> /home/user/perf/core/prof-H).
Next: count/icache of pgoT on train + held-out ROMs vs build-ref; exact.sh pgoT vs ref; then the device-like PGO
(train the full suite with build-devgen, build with -DBANANADS_PGO) and -DBANANADS_HOT_OPT=-O3 on the device build,
both for rgds-sim; layout flags; the Buildroot ship scheme write-up; then Part B (divider, ARM7 turn) as 0040+.
Scripts: /home/user/perf/core/runs/{count,icache,exact}.sh.

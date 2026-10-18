# core: status (overwritten by core only)
15:50 UTC. Branch perf/core @ 579015d (+ uncommitted: train.sh continues after a failed run). Worktree /home/user/perf/core/sd.
Done (shared): /home/user/perf/tools/build-a64-device.sh + bootlin-a55.cmake + qemu-dev; /home/user/perf/build-dev/b/
(board INFO 15:35; evidence/core/count-{ref,nossp,dev}.txt, icache-ref.txt).
PGO, GCC 13 -O3 (build-ref's compiler): trained on half the ROMs, main thread -2.7..-9.5% insns/frame on both halves
(evidence/core/count-pgoT.txt); exactness run in progress (runs/exact-pgoT.out).
Running: exact.sh ref vs build-pgoT; train-dev (device-like instrumented build /home/user/perf/core/build-devgen on
the full suite -> /home/user/perf/core/prof-dev); build-devO3 (device build with -DBANANADS_HOT_OPT=-O3).
Next: RESULT post for PGO (GCC13) once exact; dev PGO use-build (-DBANANADS_PGO=/home/user/perf/core/prof-dev, no
ccache) -> count/exact vs build-dev -> sim; count/exact of build-devO3; stale-profile experiment; layout flags
(-fno-plt, align, hot/cold) on the dev PGO build; the Buildroot ship scheme write-up; then Part B (0040+).
Scripts: /home/user/perf/core/runs/{count,icache,exact}.sh, cmp.py.

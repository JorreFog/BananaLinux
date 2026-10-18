# core: status (overwritten by core only)
16:00 UTC. Branch perf/core @ f503a7a (CMake options + train.sh). Worktree /home/user/perf/core/sd.
Done: shared device build (tools/build-a64-device.sh, qemu-dev, /home/user/perf/build-dev); RESULT PGO on GCC 13
(-2.7..-9.5% insns, exact: pictures/audio/savestates; evidence/core/count-pgoT.txt, exact-pgoT.txt); -O3 hot targets
on the device build counted (-1.8..-7.6%, L5 +0.3%; evidence/core/count-devO3.txt), exactness running.
Running: exact.sh dev vs build-devO3; train-dev (full suite -> /home/user/perf/core/prof-dev, instrumented dev build
/home/user/perf/core/build-devgen); train-dev-no3d (-> prof-dev-no3d).
Next, in order: dev PGO use-build (CCACHE_DISABLE=1 build-a64-device.sh ... -DBANANADS_PGO=/home/user/perf/core/prof-dev)
-> count + exact vs build-dev -> RESULT + sim; the no-3D and 3D-only split builds (coordinator's caveat); PGO + -O3
together; stale-profile experiment; layout flags; ship scheme write-up (findings/core.md); then Part B (0040+).

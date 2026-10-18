# core: status (overwritten by core only)
15:20 UTC. Branch perf/core @ 00c34c7 (nothing committed yet). Worktree /home/user/perf/core/sd.
Doing: Part A, PGO of the aarch64 build: adding CMake options (BANANADS_PGO_GENERATE / BANANADS_PGO=<gcda dir>),
building an instrumented binary under /home/user/perf/core/build-pgogen, training under qemu-user on the test ROMs.
Next: -fprofile-use build, A/B with qemu-count (+ libcache) and rgds-sim when ready; layout flags; then Part B
(divider, ARM7 turn, scheduler) as melonDS patches 0040+.
Runs/evidence scratch: /home/user/perf/core/runs/.

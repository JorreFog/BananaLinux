# core: status (overwritten by core only)
16:26 UTC. Branch perf/core: f503a7a (CMake PGO/layout/-O3 options + train.sh), 069a7d9 (core.fast_div, 0040, default off:
-0.4%). Worktree /home/user/perf/core/sd.
RESULTs posted: PGO GCC 13 (15:54), HOT_OPT=-O3 (16:07, sim membench -9.0%, cpubench -1.6%), PGO on the device build
(16:1x: sim cpubench -4.3%, L5 -4.1%, bigcode -3.2%); DEAD-END -fno-plt. All evidence in evidence/core/.
Running: check-toggle core.fast_div (difftests, 3 left: dmasnd, sndbench, iorom); count of the no-3D PGO build
(build-devno3d) on the 3D ROMs it never saw; build-dev3donly (3D-only profile).
Queued builds (one at a time): build-stale (prof-dev on a tree with a new function at the top of NDS.cpp:
/home/user/perf/core/build-stale/melonds-src ready), hot/cold + align on the PGO build, PGO + -O3.
Next: RESULT/DEAD-END post for 0040 when the difftests end; split results; stale result; layout levers; then the
write-up (findings/core.md has sections 0-2, 4 and the ship scheme), patches/core/, HANDOFF by 22:10.
Binaries of measured builds: /home/user/perf/core/bin/ (build dirs deleted as they finish).

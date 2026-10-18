# core: status (overwritten by core only)
16:12 UTC. Branch perf/core @ f503a7a + uncommitted: patches/melonds/0040 (core.fast_div), src/emu.cpp, docs/SETTINGS.md. Worktree /home/user/perf/core/sd.
in one step), src/emu.cpp (the setting), docs/SETTINGS.md. Worktree /home/user/perf/core/sd.
RESULTs posted: PGO on GCC 13 (15:54), BANANADS_HOT_OPT=-O3 on the device build (16:07); both sent to sim.
Running: exactness of the device PGO build vs build-dev (count done: -3.6..-21% insns, evidence/core/count-devpgo.txt; rgds-sim cpubench-thumb -4.1% ms, sim-devpgo.txt); build-b (0040 + difftest).
Split profiles trained: /home/user/perf/core/prof-dev-no3d, prof-dev-3donly (use-builds queued).
Next: RESULT for device PGO; the no-3D and 3D-only split builds (CCACHE_DISABLE=1 build-a64-device.sh ... -DBANANADS_PGO=
/home/user/perf/core/prof-dev-no3d|3donly) counted on what they never saw; count/exact/difftest/audio of 0040 (core.fast_div
on vs off) -> RESULT + commit; stale-profile experiment; PGO+-O3 together; layout levers; findings/core.md (draft exists).
Part B after 0040: RunTimers/GPU3D next-due (rgds-sim: lambda = 20.6% of main thread on cpubench; ARM_Dispatch/Ret 3.5%,
told jit), SPU mix, DMA.

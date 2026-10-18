# core: status (overwritten by core only)
16:08 UTC. Branch perf/core @ f503a7a + uncommitted: patches/melonds/0040 (core.fast_div: StartDiv/StartSqrt reschedule
in one step), src/emu.cpp (the setting), docs/SETTINGS.md. Worktree /home/user/perf/core/sd.
RESULTs posted: PGO on GCC 13 (15:54), BANANADS_HOT_OPT=-O3 on the device build (16:07); both sent to sim.
Running: count of the device PGO build (/home/user/perf/core/build-devpgo, profile prof-dev = full suite) and its
exactness vs build-dev; build-b (device build of perf/core with 0040 + difftest); training "3D only" split.
Next: RESULT for device PGO; the no-3D and 3D-only split builds (CCACHE_DISABLE=1 build-a64-device.sh ... -DBANANADS_PGO=
/home/user/perf/core/prof-dev-no3d|3donly) counted on what they never saw; count/exact/difftest/audio of 0040 (core.fast_div
on vs off) -> RESULT + commit; stale-profile experiment; PGO+-O3 together; layout levers; findings/core.md (draft exists).
Part B after 0040: RunTimers/GPU3D next-due (rgds-sim: lambda = 20.6% of main thread on cpubench; ARM_Dispatch/Ret 3.5%,
told jit), SPU mix, DMA.

# jit: status (overwritten by jit only)
Updated 15:54 UTC.
- Worktree /home/user/perf/jit/sd, branch perf/jit @ 7315d05: melonDS patches 0021 (jit.hugepages), 0022
  (MELONDS_JIT_EXITSTATS), 0023 (jit.irqchain), docs/SETTINGS.md rows, emu.cpp settings.
- Build dirs: /home/user/perf/jit/b1 (Ubuntu -O3; melonds-src git: base fb6e7dc + 0021-0023 as commits),
  /home/user/perf/jit/d1 (device build, build-a64-device.sh: applies the full series 0001-0023 cleanly).
  Patches exported by /home/user/perf/jit/export-patches.sh <builddir>.
- Exactness suite: evidence/jit/exact-suite.sh (QEMU=/home/user/perf/tools/qemu-dev for device builds).
- Done: hugepages RESULT (-0.6% cpubench, sim uncalibrated), irqchain exact + sabotage caught.
- Now: tests/bigcode (game-sized code footprint ROM) to measure hugepages / L1I in rgds-sim on the device build.
- Next candidates: VRAM/OAM/palette stores inline (PERF.md What's left #3); ExitCheck/RET-mispredict trims.
- Cycle budget (#2 in my brief): analysed, not started: ExitCheck is 230k of 2,747k JIT-code insns on cpubench thumb
  (0.335 of 5.59 ms in the sim), no free callee-saved register (x19-x29 all used), so it needs Timestamp
  materialized at every C call; est. net 2-4% on cpubench, less on games. Parked unless time is left.

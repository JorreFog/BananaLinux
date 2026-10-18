# jit: status (final; overwritten by jit only)
Updated 17:37 UTC. Work finished; findings/jit.md has everything (both parts, device commands, next ideas).

## Part 1: melonDS's AArch64 JIT (perf/jit @ 3450884, /home/user/perf/jit/sd; patches/jit/ = 4 commits)
melonDS patches 0021 jit.hugepages, 0022 MELONDS_JIT_EXITSTATS, 0023 jit.irqchain, 0024 jit.entry_x26 (all exact,
default on); test ROMs tests/irqmask and tests/bigcode (built copies /home/user/perf/jit/bin/). Dead ends: lighter
ARM_Dispatch (+3.2%), FOLDSTORE (-0.4% stores). Cycle budget: analysed, not built. JIT inline VRAM stores: handed to core.

## Part 2: 2D (perf/jit-2d @ a8725d0, /home/user/perf/jit/sd2d; patches/jit-2d/ = 3 commits; render cherry-picked all 3)
render.2d_conv16 (on), NEON FillLine, render.2d_prefetch16 (on; rests on rgds-sim v3's PRFM model).

## Tools left for whoever continues
- evidence/jit/exact-suite.sh (JIT exactness; QEMU=/home/user/perf/tools/qemu-dev for device builds)
- /home/user/perf/jit/census.py (static byte census of the JIT's code: melonjit .code dump + perf map)
- /home/user/perf/jit/export-patches.sh (melonds-src commits after "base: BananaDS patches 0001-0020" -> 00NN patches)
- attribution trick for inlined 2D code: a worktree with the functions noinline, then rgds-sim run.sh (see findings)

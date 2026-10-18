# jit: status (overwritten by jit only)
Updated 16:41 UTC.

## Branch and builds
- Worktree /home/user/perf/jit/sd, branch perf/jit @ b178c99 (commits: b0218cd 0021 hugepages, 7315d05 0022/0023,
  b178c99 tests/irqmask + tests/bigcode + 0023 self loops).
- melonDS dev repo: /home/user/perf/jit/d1/melonds-src (git: pristine, "base: BananaDS patches 0001-0020", then one
  commit per patch 0021-0023). Export: /home/user/perf/jit/export-patches.sh (default d1) -> sd/bananads/patches/melonds.
  Build: JOBS=2 BT="bananads bananads-difftest bananads-tests" /home/user/perf/tools/build-a64-device.sh /home/user/perf/jit/sd /home/user/perf/jit/d1
  (commit in melonds-src + export BEFORE building: CMake's patch check fails on uncommitted edits).
- /home/user/perf/jit/b1: older Ubuntu-flag build dir (its bananads is a SABOTAGE build; don't use; delete at the end).
- Binaries kept: /home/user/perf/jit/bin/ (bananads-d1-7315+ = d1 at b178c99; irqmask-*.nds, bigcode-*.nds; sab/).

## Done (all exact, suite FAILS=0)
- 0021 jit.hugepages (default on): sim -0.44% cpubench, -0.72% bigcode (evidence/jit/thp-sim-dev-*.txt).
- 0022 MELONDS_JIT_EXITSTATS=n (cold paths only).
- 0023 jit.irqchain (default on): sim -16.9% on tests/irqmask; 25k masked-IRQ returns/frame -> 0; sabotage caught.
- Dead end: lighter ARM_Dispatch (+3.2%): evidence/jit/deadend-litedispatch-sim.txt; patch kept in /home/user/perf/jit/dead/.
- Suite: evidence/jit/exact-suite.sh (QEMU=/home/user/perf/tools/qemu-dev for device builds; includes irqmask x3).

## Now
- CLAIM 16:41: smaller JIT code for the L1I (bigcode: jit-code IPC 0.25, 45k L1I misses/frame). Step 1: FASTENTRY
  prologue into ARM_Dispatch (x26 from the CPU), measure on bigcode + cpubench with rgds-sim ab.sh.

## Next
- Cold conditional-exit ways out of line; byte census of bigcode's JIT code; VRAM/OAM/palette inline stores.
- Cycle budget: parked (analysis in findings: ExitCheck is ~6% of cpubench, no free callee-saved register).

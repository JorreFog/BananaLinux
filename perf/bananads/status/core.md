# core: status (overwritten by core only)
16:20 UTC. Branch perf/core: f503a7a (CMake PGO/layout/-O3 options + train.sh) + 0040 commit (core.fast_div). Worktree
/home/user/perf/core/sd.
RESULTs posted: PGO on GCC 13 (15:54), BANANADS_HOT_OPT=-O3 on the device build (16:07), PGO on the device build (16:1x,
rgds-sim -4.3% cpubench-thumb, -3.2% bigcode); all sent to sim. CLAIM 0040 posted.
Running: check-toggle.sh core.fast_div (pictures/audio/difftest off vs on, runs/check-fastdiv.out); count off/on
(runs/count-b-div{off,on}.out); build-devno3d (PGO from the no-3D profile); ab.sh devpgo on dsstress-L5 and devO3 on
membench (runs/ab-*.out).
Next: RESULT 0040; the 3D-only split build (CCACHE_DISABLE=1 build-a64-device.sh ... -DBANANADS_PGO=/home/user/perf/core/
prof-dev-3donly) and counts of both splits on what they never saw; stale-profile experiment; PGO + -O3; layout
levers (align, hot/cold; -fno-plt is dead: 1.1k PLT insns/frame); findings/core.md (sections 0-2 written).
Evidence: evidence/core/*.txt. Scripts: /home/user/perf/core/runs/{count,icache,exact,check-toggle}.sh, cmp.py, summ.py.

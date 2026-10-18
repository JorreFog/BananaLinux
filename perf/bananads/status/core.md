# core: status (overwritten by core only) — 17:39 UTC, second run (end moved to 18:28; no new work from 17:50)
1. jit.gfxstores (melonDS 0041, helper version) on perf/core-jitmem @ 581f083 (worktree /home/user/perf/core/sdjit, from
   perf/jit @ 3450884): MEASURED rgds-sim v2 st: membench -0.9%, cpubench-thumb -0.7%, ds2dtest -0.06%, bigcode -1.1%
   (evidence/core/ab-gfxstores-*.txt); exact: pictures+audio off/on FAILS=0 on 5 ROMs x soft/gengis2
   (exact-gfxstores-extra.txt); jit's quick suite and a sabotage build running (runs/gfx2-suite-quick.txt, runs/sab2.log).
   Ships (patches/core-jitmem/) only if both pass. findings/core.md section 5 written.
2. PGO profile for sim's integrated tree (perf/integrated @ 5f0f214, worktree /home/user/perf/core/sdint): instrumented
   build /home/user/perf/core/build-intgen -> train.sh -> /home/user/perf/core/prof-int (runs/train-int.out, "TRAIN-DONE"
   at the end). Hard stop 18:08: if not ready, sim uses prof-dev.
3. Earlier: perf/core @ 069a7d9 exported to patches/core/ (2 patches); PGO kit state and all build-lever results in
   findings/core.md; align needs the device (board 17:05).

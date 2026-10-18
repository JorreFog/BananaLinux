# core: status (overwritten by core only)
16:36 UTC. Branch perf/core: f503a7a (CMake PGO/layout/-O3 options + train.sh), 069a7d9 (core.fast_div 0040, default
off, -0.4%). Worktree /home/user/perf/core/sd.
Posted: RESULT PGO GCC13; RESULT HOT_OPT=-O3 (sim membench -9.0%, cpubench -1.6%); RESULT device PGO (sim cpubench -4.3%,
L5 -4.1%, bigcode -3.2%); RESULT(marginal) core.fast_div; INFO slice loop = number of slices; DEAD-END -fno-plt.
Splits measured (evidence/core/count-devno3d.txt, count-dev3donly.txt, ab-devno3d-L5.txt, ab-dev3donly-membench.txt):
with Gengis (rgds-sim) never-seen ROMs still gain (-6.0%, -6.6%); qemu-count's soft renderer regresses (+2..17% in
that bucket only). Stale-profile build: 0 mismatches from an inserted function, only changed functions lose theirs.
Running: count of build-stale; ab.sh dev3donly cpubench-thumb; build-hotcold (prof-dev + BANANADS_HOTCOLD=ON).
Queued builds: align 32:16:16 on prof-dev; PGO + HOT_OPT=-O3. Then: splits RESULT post, layout RESULT/DEAD-END posts,
findings/core.md (mostly written), patches/core/, HANDOFF by 22:10.
Binaries: /home/user/perf/core/bin/. Scripts: /home/user/perf/core/runs/{count,icache,exact,check-toggle}.sh, cmp.py, summ.py.

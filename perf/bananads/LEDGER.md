# Ledger: results measured again by someone else

Only `sim` and the coordinator write here. A row starts PENDING when a RESULT is posted, and becomes VERIFIED
(re-measured from the agent's branch, same numbers within the simulator's determinism, exactness re-checked) or
REFUTED (with why).

| id | agent | change (toggle) | workload | metric before -> after | exact? | status | verified by | evidence |
|---|---|---|---|---|---|---|---|---|
| L1 | render | 2D compositor loop unrolled by a fold (`render.2d_comp_unroll`, default on) | membench, ds2dtest (dev build, 1x) | Composite at -O2, insns per 256-px call (harness): 3 steps + 2nd line 3966 -> 1558; 4 steps 5665 -> 1833 (-O3 plain: 1432 / 1833) | pending (render ran its harness) | PENDING | - | evidence/render/comp-bench.txt; perf/render @ 968fe03 |
| L2 | jit | JIT code memory on 2 MB huge pages (melonDS 0021, `jit.hugepages`, default on) | cpubench-thumb (Ubuntu -O3 build, rgds-sim v1 uncalibrated) | main 5.592 -> 5.557 ms/frame @1104 (-0.6%); dTLB L2 hits 37.6k -> 27.6k | suite FAILS=0 (jit) | PENDING | - | evidence/jit/thp-sim-cbthumb.txt; perf/jit @ b0218cd |
| L3 | jit | masked IRQs don't end block chains (melonDS 0023, `jit.irqchain`) | test ROMs hit the case ~0.7/frame: no sim gain expected; device A/B | n/a here | suite FAILS=0; sabotage caught (FAILS=4) | PENDING (exactness only) | - | evidence/jit/exact-irqchain*.txt; perf/jit @ 7315d05 |

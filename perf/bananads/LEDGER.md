# Ledger: results measured again by someone else

Only `sim` and the coordinator write here. A row starts PENDING when a RESULT is posted, and becomes VERIFIED
(re-measured from the agent's branch, same numbers within the simulator's determinism, exactness re-checked) or
REFUTED (with why).

| id | agent | change (toggle) | workload | metric before -> after | exact? | status | verified by | evidence |
|---|---|---|---|---|---|---|---|---|
| L1 | render | 2D compositor loop unrolled by a fold (`render.2d_comp_unroll`, default on) | membench, ds2dtest (dev build, 1x) | Composite at -O2: 2588 -> 1558 insns per 256-px line (2 BG steps), harness | pending (render ran its harness) | PENDING | - | evidence/render/comp-bench.txt; perf/render @ 968fe03 |

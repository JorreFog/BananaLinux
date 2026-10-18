# Ledger: results measured again by someone else

Only `sim` and the coordinator write here. A row starts PENDING when a RESULT is posted, and becomes VERIFIED
(re-measured from the agent's branch, same numbers within the simulator's determinism, exactness re-checked) or
REFUTED (with why).

| id | agent | change (toggle) | workload | metric before -> after | exact? | status | verified by | evidence |
|---|---|---|---|---|---|---|---|---|

# jit: status (overwritten by jit only)
Updated 15:12 UTC.
- Worktree /home/user/perf/jit/sd, branch perf/jit @ 00c34c78e8de (no commits yet).
- Build dir /home/user/perf/jit/b1 (base built; melonds-src has commit fb6e7dc "base: 0001-0020" on top of pristine).
- Now: idea 1, the JIT code buffer on 2 MB huge pages (MADV_HUGEPAGE + 2 MB alignment), toggle jit.hugepages /
  MELONDS_JIT_HUGEPAGE. Then idea 2 (cycle budget in a register).
- Patch numbers: 0021-0039.

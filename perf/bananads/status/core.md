# core: status (overwritten by core only) — FINAL 17:52 UTC (second run)
Shipped: patches/core/ (perf/core @ 069a7d9: BANANADS_PGO/PGO_GENERATE/HOT_OPT/ALIGN/HOTCOLD/NOPLT/STACK_PROTECTOR + train.sh;
core.fast_div off) and patches/core-jitmem/ (perf/core-jitmem @ 581f083 on perf/jit @ 3450884: melonDS 0041 jit.gfxstores,
exact, -0.7..-1.1%). PGO profile for perf/integrated @ 5f0f214: /home/user/perf/core/prof-int (sim has it; 17:50).
Worktrees: /home/user/perf/core/sd (perf/core), sdjit (perf/core-jitmem), sdint (5f0f214, detached), sd2d (unused).
findings/core.md is final (sections 0-5, ship scheme, kit state, device A/B, dead ends); evidence/core/*.
Next for a later session: the kit's scripts (findings "where it stands"), the slow loads/VCOUNT reads via helpers,
the full exact-suite on 0041, the device A/Bs (PGO, align, gfxstores).

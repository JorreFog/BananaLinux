# core: status (overwritten by core only) — 17:17 UTC, second run
Now: the JIT's VRAM/OAM/palette stores inline (melonDS patch 0041, jit.gfxstores; board CLAIM 17:05). Worktree
/home/user/perf/core/sdjit, branch perf/core-jitmem (from perf/jit @ 3450884); the patch is in the tree (uncommitted until
exact + measured): bananads/patches/melonds/0041-...patch, src/emu.cpp (jit.gfxstores -> MELONDS_JIT_NOGFXSTORES),
docs/SETTINGS.md. Build: /home/user/perf/core/build-jitmem-base (melonds-src edited in place; git commit "patched 0001-0024"
= the base; base binaries in /home/user/perf/core/bin/bananads-jitbase). Running: runs/gfx-ab.sh (rgds-sim off vs on,
4 ROMs) then runs/gfx-exact.sh (jit's exact-suite + 8 ROMs pictures/audio); a sabotage build (no VRAM dirty bit) in
build-sab. Smoke: membench pictures+audio identical off/on.
Earlier today (perf/core @ 069a7d9, patches/core/): PGO/HOT_OPT/align/hotcold results all posted; the PGO device kit's
state is in findings/core.md (section "where it stands"); align needs the device (board 17:05).
Next: results -> board RESULT + LEDGER (sim), commit, findings section, patches/core-jitmem/; then inline VRAM/OAM
loads (ReadVRAMQuick) if time permits.

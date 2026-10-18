# render: status (overwritten by render only) — HANDOFF-ready; updated 17:36 UTC (date -u); every check finished (CHAIN5-DONE 17:41); the session ends 18:28 UTC

## Where things are
- Branch `perf/render` @ 2ede30e (HEAD; the code head is dc50d19, 2ede30e is PERF.md only), worktree /home/user/perf/render/sd,
  from 00c34c78e8de. 18 commits, every change behind a toggle, exact. Mine: 7ebe4e9 render.2d_batch; ea886e8
  render.3d_pipeline; dc3f96a 3d_share/3d_wait2d at 1x; 911ff8e render.2d_comp_unroll; 0bab5aa render.2d_quarters; 2a23af3
  render.2d_emit_zip; 347784d the quarters merge fix; f76bfca docs/PERF.md; 1c9095b the Gengis workers' wake condition;
  8677fad render.2d_fence_wait; eee302e render.2d_prefetch (off); dc02f53 render.2d_job_stride; 4d1ee7c render.2d_win_neon;
  0b5346b render.2d_steps_cache; 2ede30e PERF.md. jit's (perf/jit-2d, cherry-picked 17:31): 94ee0ea render.2d_conv16 +
  render.2d_prefetch16; ea7732d FillLine NEON; dc50d19 prefetch16 on by default. melonDS patch 0060 (geom_sort) was
  written, measured at -0.1%, rebased out (evidence/render/0060-geom-sort-dropped.patch). 0060-0069 unused.
- Deliverables: patches/render/ (18 patches from 2ede30e; `git am` onto 00c34c78e8de gives the branch exactly, checked);
  findings/render.md (what, why, numbers, checks, device A/B lines, risks, next, dead ends); evidence/render/ (every
  number's file: see its README-like headers; prof-head/ the sim profiles, st1x-layout/ the L1I series, sim/ the A/Bs).
  Re-export after any new commit: `rm patches/render/*.patch; git -C /home/user/perf/render/sd format-patch
  00c34c78e8de..perf/render -o /home/user/BananaLinux/perf/bananads/patches/render/`.
- Builds (device toolchain, build-a64-device.sh): /home/user/perf/render/b15d = dc50d19 (the integrated head: measure this
  one), b13d = 4d1ee7c, b10d = eee302e, b11a = eee302e without LTO (attribution; binary only). Base = /home/user/perf/build-dev/b.
  Run with /home/user/perf/tools/qemu-dev. x86tsan = TSan x86 (the 2D thread's code only, no Gengis). My ROM:
  /home/user/perf/render/roms/membench-win.nds (membench + WIN0 over the 3D scene; source evidence/render/membench-win-main9.c).
- Measured (rgds-sim v2 unless said; device builds): comp_unroll membench -5.1% / ds2dtest -5.9% of the emulator's thread
  (sim VERIFIED, L1); pipeline: the emulator's thread -27% at 2x (sim VERIFIED for 2x, L5; the device verdict is the
  wait time); 1x share -18%; emit_zip: the 2D thread -7.1%; quarters: its composite work -28%; fence wait: device
  verdict (kms.stats); prefetch: device verdict; job stride: the 2D thread's loop -36% (L5), dTLB walks -62%, the thread
  inside dev noise; win_neon: membench-win the 2D thread -47% (2x), the emulator's thread -31% (st 2x) / -18% (1x);
  steps cache: A/Bs on b15d running at the end (runs/sim/*steps*). Head vs base on L5 2x (b10d): the 2D thread 4.765 ->
  3.855 ms/frame, the emulator's thread 4.355 -> ~3.2.
- Exactness: matrices m3 (41 configs), m2 (8), m6 (23), m7 (18), m9 (15), m10 (10), m12 (23): FAILS=0 every time; m13
  (win_neon, b13d) 11 configurations FAILS=0, m15 (the integrated head, b15d) 17 configurations FAILS=0
  (evidence/render/matrix-m13.txt, matrix-m15.txt); difftest --every toggles off vs on SAME (L5,
  ds2dtest, geomtest); bananads-tests pass; unit tests comp_bench / quarters_test (20000) / zip_test (6000) / win_test
  (5000 x 17) 0 mismatches; TSan 13 runs 0 data races.

## The 2D plan (16:52 on) and my share
- 2D-PLAN.md: all agents on the 2D engine. My share: the compositor (Composite*, BuildSteps, Effects*/Brightness*,
  DrawBG_3D, CompositeTo/EffectsTo, DrawScanline*, the 2x pass structure, render.2d_prefetch), sprites, windows,
  mosaic, palettes, PrepareScanline*, the copy log, the job ring, the Gengis-help question (answered on the board
  17:10: the help is the 2D thread working instead of waiting; on the device at 60 fps the workers get VBlank to get
  ahead; the check is the `[gengis]` log line with render.gengis_help on vs off), and the integration of perf/jit-2d
  (done 17:31 for its three commits; later jit commits stay on perf/jit-2d unless cherry-picked the same way: the
  conflicts are adjacent toggle declarations in GPU2D_Banana.h/.cpp and emu.cpp, keep both sides).
- The coordinator's 1x L1I question: closed on the board 17:32 (a link-layout reshuffle: evidence/render/st1x-layout/,
  prof-head/1x-L5-l1i-diff.txt; the robust fix is a build flag, BANANADS_ALIGN or PGO, core's).
- Shelved: render.2d_tiles (evidence/render/tiles-shelved.diff; it edits jit's output stage; small gain).

## Running at the end (fold in if you take over)
- runs/chain3.sh: ds2dtest 1x win_neon A/B (-> runs/sim/st1x-ds2d-winneon-ab.txt), then matrix m13 on b13d.
- runs/chain5.sh: waits for b15d and chain3, then m15 (base vs b15d, 18 configurations incl. membench-win and every new
  toggle off at once) -> runs/matrix-m15.txt, then the steps-cache A/Bs (st 2x membench/ds2dtest, st 1x membench) ->
  runs/sim/*-steps-ab.txt. Copy both into evidence/render/, put the numbers into findings/render.md ("render.2d_steps_cache"
  and "Exactness checks"), post a RESULT for the steps cache and m15 on the board.

## If you take over
Read findings/render.md first, then `grep "· render ·" BOARD.md`. The open items are device A/Bs (findings, "Device A/B
commands"): the pipeline off vs on, render.2d_thread at 1x, the 2D-phase toggles' per-thread CPU time, the prefetches,
BANANADS_ALIGN/PGO. Code ideas left, in order: display capture on the 2D thread; a smaller 3D pixel format (the 2D
thread's compulsory 768 KB/frame read at 2x); rotscale sprites and the mosaic loops (scalar, ds2dtest-heavy).

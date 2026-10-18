# jit: status (overwritten by jit only)
Updated 17:27 UTC.

## Part 1 (JIT, finished 16:54): perf/jit @ 3450884, patches/jit/ exported, findings/jit.md written.

## Part 2 (2D, since 16:56): worktree /home/user/perf/jit/sd2d, branch perf/jit-2d (from perf/render eee302e)
- Commits: 60a26a9 render.2d_conv16 (on), e3c69b1 NEON FillLine, a8725d0 render.2d_prefetch16 default on.
- Build dirs: /home/user/perf/jit/d2 (device build of sd2d), /home/user/perf/jit/d2g (same + -g, for attribution
  with addr2line; code ~identical). Binaries kept in /home/user/perf/jit/bin/{c16,fl}/.
- Exactness: render's evidence/render/matrix.sh with QEMU=/home/user/perf/tools/qemu-dev (26 configs, FAILS=0 for conv16);
  unit test evidence/jit/2d-conv16_test.cpp.
- Scope now (coordinator 17:22): the BG layers (DrawBG_Text/Affine/Extended/Large, DrawBitmap8/16, DrawTiledScroll) +
  ConvertRun16. Target: ds2dtest: DrawScanlineFrom 14.4 ms/frame st 2x (11.7M insns; ~150k L1D load misses/frame, mostly
  L3 hits on the VRAM flats). Next: per-line attribution in DrawScanlineFrom (d2g + addr2line on rgds-sim's per-TB
  counts, run /home/user/perf/jit/sim3/d2t-st2x-g), then cut instructions/misses in the hottest BG kind.
- Judge 2D changes on st 2x (--mode st --set render.scale=2: deterministic); prefetch with SIMARGS=prfm=1 (v3).

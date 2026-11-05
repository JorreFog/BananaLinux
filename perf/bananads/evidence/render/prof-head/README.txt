# rgds-sim profiles (model v2, frames 100-300) of the head build b10d = perf/render @ eee302e (device toolchain, LTO), MEASURED:
#   head-L5-dev: dsstress-L5, dev mode (2x, gengis, 2 Gengis threads, 2D thread) -- the 2D thread's per-function profile at 2x
#   head-ds2dtest-st, head-membench-st: st mode (1x, everything on the emulator's thread)
#   head-mwin-dev, head-mwin-st: membench-win (membench with WIN0 over the 3D scene; ../membench-win-main9.c), 2x dev and 1x st
#   1x-base-L5, 1x-head-L5: dsstress-L5 at render.scale=1 in dev mode, build-dev (base 00c34c7) vs b10d: the L1I question
#   attr-ds2dtest-st: ds2dtest at 1x on the attribution build b11a (eee302e without LTO, -fno-inline-functions-called-once):
#     DrawScanlineFrom's inlined pieces apart (the BG layers: jit's area since 17:1x)
# cmd: OUT=... rgds-sim/run.sh <bin> <rom> <label> --mode st|dev --frames 100:300 [--set render.scale=1]; the per-symbol
# L1I diff of 1x-base vs 1x-head: /home/user/perf/render/runs/l1i-diff.py (copied here as l1i-diff.py)

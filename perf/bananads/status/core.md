# core: status (overwritten by core only) — FINAL (core's run ended early, 16:4x UTC)
Branch perf/core @ 069a7d9: f503a7a (CMake options BANANADS_PGO/PGO_GENERATE/PGO_DIR/PGO_PARTIAL, STACK_PROTECTOR, NOPLT,
ALIGN, HOTCOLD, HOT_OPT + tools/perf/pgo/train.sh), 069a7d9 (core.fast_div, melonDS 0040, default off). patches/core/ has
both. findings/core.md is written (sections 0-4, ship scheme, device A/B commands); evidence/core/*.txt (28 files).
Shared tools: /home/user/perf/tools/build-a64-device.sh, bootlin-a55.cmake, qemu-dev; /home/user/perf/build-dev/b/.
Profiles: /home/user/perf/core/prof-dev (full suite, GCC 14.3, the one to ship/merge), prof-dev-no3d, prof-dev-3donly,
prof-T/prof-H (GCC 13). Binaries of every measured build: /home/user/perf/core/bin/ (devpgo, devO3 dirs kept for sim).
Unfinished: (1) ab.sh devpgo vs build-hotcold (BANANADS_HOTCOLD=ON) on cpubench-thumb: result lands in
/home/user/perf/core/runs/sim/hotcold-cpubench-thumb-ab.txt (binary bin/bananads-hotcold). (2) build-align (prof-dev +
BANANADS_ALIGN=32:16:16) building in /home/user/perf/core/build-align; then ab.sh vs build-devpgo. (3) PGO + HOT_OPT=-O3
together: not built (CCACHE_DISABLE=1 JOBS=2 build-a64-device.sh /home/user/perf/core/sd <dir> -DBANANADS_PGO=
/home/user/perf/core/prof-dev -DBANANADS_HOT_OPT=-O3). (4) findings/core.md section 3's hot/cold+align numbers, Dead ends
list (-fno-plt; fast_div marginal), and the HANDOFF are on the board instead.

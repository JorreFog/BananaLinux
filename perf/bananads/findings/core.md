# core: findings (the build, and the C++ core's hot paths)

Branch `perf/core` (worktree `/home/user/perf/core/sd`), patches in `patches/core/`. Everything is a CMake option or
a setting, default off unless said; the emulation is byte for byte the same with each (pictures, audio, savestates).
Numbers are qemu-count instruction counts unless marked rgds-sim; every number has a file in `evidence/core/`.

## 0. The device's build is not the PC reference build (READ, then MEASURED)

BananaLinux's Buildroot 2026.02.3 builds BananaDS with the Bootlin aarch64 glibc **stable 2025.08-1** toolchain:
**GCC 14.3.0**, binutils 2.43.1, glibc 2.41 (`banana_rgds_defconfig`: `BR2_TOOLCHAIN_EXTERNAL_BOOTLIN_AARCH64_GLIBC_STABLE`;
buildroot `toolchain-external-bootlin.mk:16`). Its package build is **`-O2 -g0 -D_FORTIFY_SOURCE=1 -DNDEBUG`** (Buildroot's
`toolchainfile.cmake` empties CMake's Release flags and sets `CMAKE_C_FLAGS` to `TARGET_CFLAGS`), no stack protector
(`BR2_SSP_NONE`), no PIE, `-Wl,-z,relro`, `-mcpu=cortex-a55`, LTO through gcc-ar; libstdc++ dynamic. The PC reference
builds here (`build-a64.sh`, Ubuntu's GCC 13.3) were **-O3** with Ubuntu's default `-fstack-protector-strong
-fstack-clash-protection -fPIE`.

Shared tools (in `/home/user/perf/tools/`): `build-a64-device.sh <tree> <dir> [-D...]` (the device's toolchain and
flags exactly, headless; `bootlin-a55.cmake`), `qemu-dev` (qemu-aarch64 with the toolchain's sysroot). The base's
device-like reference: `/home/user/perf/build-dev/b/`. Main-thread instructions, device build vs the -O3 reference:
cpubench +1.8%, membench +6.2% (2D +37%: one loop, `Composite`, which render fixed), dsstress-L5 -0.6%, ipcrom +1.9%
(`evidence/core/count-dev.txt`); Ubuntu's stack protector alone is 0.4-2.8% of the reference's count
(`count-nossp.txt`). Trap: GCC 14's LTO inlines `RunSlicesJIT` into `main`'s lambda, so qemu-count's "core" bucket
shows as "other" on device builds.

## 1. Profile-guided optimisation (CMake `BANANADS_PGO_GENERATE`, `BANANADS_PGO=<dir>`, `BANANADS_PGO_PARTIAL`)

Commit `BananaDS: build options for profile-guided optimisation ...` (perf/core). `-DBANANADS_PGO_GENERATE=ON` builds
instrumented (`-fprofile-generate` into `BANANADS_PGO_DIR`, `-fprofile-update=atomic` for the threads, profiles named
relative to the build directory with `-fprofile-prefix-path`, the source trees mapped to fixed names with
`-fprofile-prefix-map`); `tools/perf/pgo/train.sh <instrumented bananads> <roms> <profile dir> [<out dir>]` runs the
test ROMs under qemu-user (each at Gengis 1x, Gengis 2x + the 2D thread, and the soft renderer, 300 frames);
`-DBANANADS_PGO=<dir>` builds with the profiles (`-fprofile-use`, `-fprofile-partial-training` unless
`BANANADS_PGO_PARTIAL=OFF`, missing/mismatched profiles as warnings). Default off: the build is as before.

MEASURED, main-thread instructions per frame (qemu-count, frames 100-300):
- GCC 13 -O3 (build-ref's compiler), profile from half the ROMs, tested on both halves: cpubench-thumb -3.9%,
  cpubench-arm* -4.6%, thumb-arm7 -3.6%, membench -2.7%, dsstress-L5 -7.6%, L10* -8.4%, arm7-thumb-L5* -7.4%,
  sndbench -8.1%, ipcrom -9.5%, ds2dtest -4.3% (* held out, but of the same kinds as trained ones;
  `evidence/core/count-pgoT.txt`). JIT-generated code is untouched; the gain is the C++ side (RunSlicesJIT 904k ->
  867k, 2D 461k -> 372k on cpubench). Exact: pictures and audio identical on 13 ROMs x 2 renderers, savestates
  (19 MB, main RAM included) identical at frame 250 (`exact-pgoT.txt`).
- The device's compiler (Bootlin GCC 14.3, Buildroot's -O2), profile from the full suite, vs `/home/user/perf/build-dev`:
  cpubench-thumb -3.9%, cpubench-arm -4.4%, thumb-arm7 -3.6%, membench -8.2%, dsstress-L5 -8.8%, L10 -8.8%,
  arm7-thumb-L5 -8.7%, sndbench -9.2%, ipcrom -9.5%, ds2dtest -21.0%; .text 1.95 -> 2.21 MB (`count-devpgo.txt`).
- rgds-sim v1 (A55 time, st mode), device build -> device PGO: cpubench-thumb main thread 5.649 -> 5.406 ms/frame
  @1104 MHz (-4.3%), L1I misses -5.7%, iTLB L2 hits -31% (`ab-devpgo-cpubench-thumb.txt`); jit's game-sized bigcode
  ROM (6 MB of JIT code, L1I = 40% of cycles) 10.18 -> 9.85 ms (-3.2%), the C++ side's L1I misses 44.0k -> 41.4k/frame
  (`sim-bigcode-devpgo.txt`). The JIT code's own L1I misses are the same in both: PGO can't touch them.
- (the no-3D / 3D-only split builds: below, when measured)

## 2. The hot targets at -O3 under Buildroot's -O2 (CMake `BANANADS_HOT_OPT=-O3`)

Same commit. `-DBANANADS_HOT_OPT=-O3` compiles melonDS's core, the Banana 2D engine, Gengis and the frontend at -O3
after Buildroot's -O2 (the last -O wins; teakra, slirp and the tests stay -O2). MEASURED vs `/home/user/perf/build-dev`
(qemu-count): cpubench-thumb -1.8%, cpubench-arm -2.0%, membench -7.6% (its 2D bucket -31%: `Composite`, which render
has since fixed at -O2 with `render.2d_comp_unroll`), dsstress-L5 +0.3%, sndbench -2.9%, ipcrom -6.4%; .text 1.95 ->
2.25 MB (+15%) (`count-devO3.txt`). Exact: pictures and audio identical on 13 ROMs x 2 renderers (`exact-devO3.txt`).
rgds-sim: (below, when measured). Default: off until the device says (instructions vs L1I).

## 3. Layout levers (CMake `BANANADS_NOPLT`, `BANANADS_ALIGN=F:L:J`, `BANANADS_HOTCOLD`, `BANANADS_STACK_PROTECTOR`)

(filled in below as measured)

## How PGO would ship (the scheme)

A `.gcda` profile is tied to (a) GCC's major.minor (GCC checks the gcov version string: a 13.3 profile is ignored
by 14.3 with a warning), (b) each function's control-flow checksum (a function whose code changed loses its
profile: `-Wcoverage-mismatch`, a warning with `-Wno-error=coverage-mismatch`, nothing else is affected) and
(c) the function's definition order within its file and its source line (the `funcdef_no` ident and a line/path
checksum): an edit that shifts the functions after it in a file leaves their counts mismatched, so they are
compiled as if unprofiled. With `-fprofile-partial-training` (kept on) an unprofiled or never-run function is
optimised as usual, not for size; without it such functions are treated as cold (smaller, slower). So a stale
profile degrades gracefully, file by file, down to today's build, never below it; the price of a source change is
that the changed files lose their profile until it is regenerated.

The reproducible scheme, as implemented on `perf/core`:
1. The profile set is named by object path relative to the build directory (`-fprofile-prefix-path`) with the two
   source trees mapped to fixed names (`-fprofile-prefix-map`), so the same `.gcda` files serve any build directory,
   a developer's or Buildroot's `output/build/bananads-<hash>/buildroot-build/`, as long as the CMake target layout
   is the same (it is: `melonds/src/CMakeFiles/core.dir/...`, `CMakeFiles/banana2d.dir/...`). Only `bananads-net`'s
   three objects embed an absolute path (their sources are out of tree); they are cold.
2. Regeneration: `tools/perf/pgo/train.sh` runs the test ROM suite under qemu-user (Buildroot has `host-qemu` with
   `BR2_PACKAGE_HOST_QEMU_LINUX_USER_MODE`, qemu 10.2) with an instrumented build and merges the counts; sets can be
   merged with `gcov-tool merge`. Each ROM at Gengis scale 1 and 2 (2 threads + the 2D thread) and once with the
   soft renderer; ~15 minutes on a PC.
3. Two ways to wire it into BananaLinux (`bananads.mk`):
   - **Committed profile** (simplest, deterministic): the `.gcda` set (about 1 MB) generated with the Bootlin GCC
     14.3 toolchain lives in the SuperDrastic repo under `bananads/pgo/gcc-14.3/`, and `bananads.mk` passes
     `-DBANANADS_PGO=$(@D)/bananads/pgo/gcc-14.3`. A toolchain bump (Buildroot's next Bootlin release) or a source
     change means re-running `train.sh` with that toolchain (a Makefile target in BananaLinux: build instrumented
     with `BANANADS_PGO_GENERATE=ON` using the same `output/host` toolchain, train with `output/host/bin/qemu-aarch64`,
     copy the profiles back). The build log counts the `coverage-mismatch` warnings: the number of functions that
     lost their profile.
   - **Two-pass Buildroot build**: a `bananads-pgo` host-side step: configure with `BANANADS_PGO_GENERATE=ON`, build,
     run `train.sh` with `host-qemu` on the ROMs (the test ROMs would have to be shipped prebuilt in the package
     dir: `build.py` needs clang + ld.lld, which Buildroot's host doesn't have), then the real configure with
     `BANANADS_PGO=<dir>`. Always fresh, never stale, but ~25 minutes more per image build and a qemu host build.
   The committed profile is the recommendation: Buildroot's offline builds stay simple, the profile is reviewed
   like code, and staleness is visible (the warning count) and harmless.

## Device A/B commands

(filled in below)

## Dead ends

(filled in below)

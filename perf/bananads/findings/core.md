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

(filled in below as measured)

## 2. The hot targets at -O3 under Buildroot's -O2 (CMake `BANANADS_HOT_OPT=-O3`)

(filled in below as measured)

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

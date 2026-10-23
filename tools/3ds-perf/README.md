# 3ds-perf: measuring Panda3DS for the RG DS Plus without the handheld

The RG DS Plus has an RK3568: four Cortex-A55 cores at 2.0 GHz, a Mali-G52 2EE and 1 GB of RAM. A 3DS game at 60
frames a second leaves the emulator 16.7 ms a frame. These tools put a number on how much of that a build of
Panda3DS uses, on a PC, the same way every time:

| | |
|---|---|
| `../3ds-bench/` | the stress-test ROM: five scenes of 300 frames (`cpu`, `3d`, `2d`, `mixed`, `fill`), each announcing itself with `svcOutputDebugString("BENCH scene=...")` |
| `panda3ds-bench` | Panda3DS with no window (`src/panda_bench` in BananaOS's Panda3DS branch): runs a ROM for N frames as fast as it can, `--renderer null` or `gl` (OpenGL ES 3.1, EGL surfaceless), prints each scene's frame times and the peak memory |
| `a55prof.c` | a QEMU TCG plugin for `qemu-aarch64`: the AArch64 instructions the bench executes, per function, per frame (the bench marks frames with a syscall QEMU passes to the plugin) |
| `a55report.py` | turns that into Cortex-A55 milliseconds per frame and scene, where they go (guest code, the JIT, the GPU emulation, the renderer, the GL driver...), and a comparison with another run |
| `rgds-plus-vm.sh` | the RG DS Plus as a QEMU machine (4 Cortex-A55, 1 GB): boots a kernel and an initramfs with the bench and runs it, so the build is proven on the A55's instruction set and inside 1 GB |

## The cost model

QEMU translates the A55's code on the PC, so its clock says nothing about the handheld. What it can count exactly is
the instructions the A55 would run. `a55report.py` turns them into time at the A55's 2.0 GHz with a fixed CPI
(1.25 cycles per instruction by default, `--cpi`): the A55 issues in order, two instructions a cycle at best, and an
emulator's code (loads, branches, calls) does well to average 0.8 a cycle. The model is the same for every build, so
comparisons between builds are comparisons of work; the absolute numbers want checking on the handheld (BananaOS's
`kms.stats`-style counters, or `perf stat` there, give the real CPI).

Only the emulator's own thread is costed: other threads (Mesa's llvmpipe rasterizing, which is the Mali's work on the
handheld; the audio thread) are one line per scene. With `--renderer gl` the GL driver's own CPU work is Mesa's
(llvmpipe's front end here, panfrost's on the handheld), so it is shown on its own line.

## Running it

```sh
# once: QEMU 9.2 with plugins (Ubuntu's qemu-user has none), the cross compiler, arm64 Mesa (multiarch)
git clone --depth 1 --branch v9.2.4 https://gitlab.com/qemu-project/qemu.git && cd qemu && mkdir build && cd build &&
  ../configure --target-list=aarch64-linux-user,aarch64-softmmu --enable-plugins --without-default-features --enable-slirp &&
  make qemu-aarch64 qemu-system-aarch64
gcc -O2 -shared -fPIC -I qemu/include/qemu $(pkg-config --cflags --libs glib-2.0) a55prof.c -o liba55prof.so

# panda3ds-bench for the A55 (the Panda3DS branch, cross-compiled with -mcpu=cortex-a55 -DBUILD_BENCH=ON)
qemu-aarch64 -plugin ./liba55prof.so,out=opt.prof panda3ds-bench 3ds-bench.3dsx --frames 1500 --renderer null --marks --maps opt.maps
PANDA3DS_BASELINE=1 qemu-aarch64 -plugin ./liba55prof.so,out=base.prof panda3ds-bench 3ds-bench.3dsx --frames 1500 --renderer null --marks --maps base.maps
./a55report.py opt.prof --maps opt.maps --compare base.prof --compare-maps base.maps

# the RG DS Plus VM: runs the bench inside 4 A55s and 1 GB, prints its table and the memory left
./rgds-plus-vm.sh panda3ds-bench 3ds-bench.3dsx --frames 600 --renderer null
```

`PANDA3DS_BASELINE=1` switches every one of BananaOS's changes off at run time (the code paths upstream Panda3DS
runs), so one binary gives both sides of a comparison.

## Results (October 2026, the stress-test ROM as committed)

The emulator's own thread, Cortex-A55 milliseconds a frame (CPI 1.25), upstream Panda3DS (`PANDA3DS_BASELINE=1`)
against BananaOS's patches, the same binary:

| scene | ARM11 busy | null renderer: upstream | BananaOS | GL: upstream | BananaOS |
|---|---:|---:|---:|---:|---:|
| cpu | 80% | 8.2 | 7.1 | 41.6 | 7.0 |
| 3d | 71% | 19.8 | 7.5 | 78.0 | 6.4 |
| 2d | 72% | 17.2 | 5.8 | 28.3 | 5.7 |
| mixed | 59% | 17.1 | 5.6 | 33.4 | 5.1 |
| fill | 12% | 12.2 | 1.2 | | |

With GL, BananaOS's renderer and GL driver run on the GPU thread: Panda3DS's part of it was 0.3 to 1.6 ms a frame;
the GL driver's part here is llvmpipe's (it shades vertices on that thread, the Mali's work on the handheld), so
panfrost's cost there wants measuring on the handheld (`tools/panda3ds-device`).

# Panda3DS on the handheld, without building an image

Prebuilt for aarch64 (Cortex-A55), for testing BananaOS's Panda3DS work on a real RG DS Plus (or RG DS) that runs
BananaOS already. An image built with `buildroot/package/libretro-panda3ds` has all of this in it; this folder is for
trying it before that.

| | |
|---|---|
| `panda3ds_libretro.so.xz` | the RetroArch core (what the 3DS shelf runs), with the GPU on its own thread |
| `panda3ds-bench.xz` | Panda3DS with no window: runs a ROM for N frames as fast as it can, prints each scene's frame times |
| `install-on-device.sh` | puts both, and the stress-test ROM (`../3ds-bench/3ds-bench.3dsx`), in `/data/panda3ds`, and the 3DS on the shelf |

Built from Panda3DS `92e53d26` with `buildroot/package/libretro-panda3ds/*.patch` (branch `banana3ds`), with
`-mcpu=cortex-a55`, C++ runtime linked in: they need only glibc 2.38 or later and the image's `libEGL.so.1`
(panfrost).

## Installing

Copy `tools/panda3ds-device` and `tools/3ds-bench` to the card (file sharing, or `scp -r` over SSH: Settings >
System > SSH), then on the handheld:

```sh
sh /data/tools/panda3ds-device/install-on-device.sh       # wherever the folder landed
```

Restart the menu: the Nintendo 3DS is on the shelf, its games in `/data/roms/3ds` (decrypted `.3ds`/`.cci`/`.cxi`, or
`.3dsx` homebrew). `install-on-device.sh remove` takes it all away again.

## Measuring

The benchmark wants the game's clocks, not the menu's, and nothing else drawing:

```sh
banana-perf performance
cd /data/panda3ds
./panda3ds-bench 3ds-bench.3dsx --renderer gl --frames 1500                     # BananaOS's build
PANDA3DS_BASELINE=1 ./panda3ds-bench 3ds-bench.3dsx --renderer gl --frames 1500 # upstream's code paths, for comparison
./panda3ds-bench 3ds-bench.3dsx --renderer gl --frames 1500 --gpu-thread off   # the GPU on the emulator's thread
./panda3ds-bench 3ds-bench.3dsx --renderer null --frames 1500                   # the CPU side alone
banana-perf ui
```

Each prints a table: per scene (`cpu`, `3d`, `2d`, `mixed`, `fill`, 300 frames each), the milliseconds a frame took
(mean, median, 95th percentile, worst; 16.7 ms is 60 frames a second) and how busy the emulated ARM11 was. The run
is as fast as the handheld goes (no frame pacing), so under 16.7 ms means the scene would hold 60. `--dump <dir>`
also writes each scene's last frame as a PNG, to check the picture (it slows the run; don't time with it).

The numbers to send back: the four tables, `cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq` during a run,
and, from a game on the shelf, RetroArch's frame rate (its menu's Settings > On-Screen Display > Show Frame Rate).

## What the PC measured, to compare against

The emulator's own thread on a Cortex-A55 model (instructions counted in QEMU, 1.25 cycles each at 2.0 GHz, see
`tools/3ds-perf`), the stress-test ROM, milliseconds per frame:

| scene | upstream | BananaOS | |
|---|---:|---:|---:|
| cpu (null renderer) | 8.2 | 7.1 | 1.15x |
| 3d (null renderer) | 19.8 | 7.5 | 2.7x |
| 2d (null renderer) | 17.2 | 5.8 | 3.0x |
| mixed (null renderer) | 17.1 | 5.6 | 3.1x |
| fill (null renderer) | 12.2 | 1.2 | 9.9x |
| cpu (GL) | 13.3 | 7.2 | 1.9x |
| 3d (GL) | 81.3 | 6.6 | 12x |
| 2d (GL) | 28.5 | 5.9 | 4.9x |

With the GL renderer, upstream's numbers include the renderer and the GL driver on the same thread; BananaOS's run
them on the GPU thread (its own core on the handheld), whose cost with panfrost only the handheld can tell. The model's
1.25 cycles per instruction is a guess the handheld will correct.

# Banana for the Retroid Pocket Classic — codebase analysis

**Target:** the Retroid Pocket Classic (Qualcomm Snapdragon G1 Gen 2 / SM4450, Rev Q): one 1240×1080 AMOLED panel,
a 6×Cortex-A55 + 2×Cortex-A78 SoC, an Adreno a613, the AudioReach DSP with a WCD937x codec and two AW88166
amplifiers, a fan, and no lid. **Version:** 0.4.2. No submodules; the hardware layer is imported into
`buildroot/board/retroid-pocket-classic` (`docs/RPC-PORT.md` is the port's log).

## 1. Layout

| Path | Subsystem | Role |
|---|---|---|
| `ui/` | **Frontend / home menu** | `banana-ui` (C++17): KMS host (`src/`), imported render engine (`rnds/`), assets, headless test |
| `system/bananad/` | **Power daemon** | Sleep/power/LEDs/sound-routing/brightness/battery (`bananad.c`) and its namespaced test |
| `system/rootfs/` | **Init + rootfs overlay** | `init.d/rcS`, `inittab`, `banana-perf`, the launchers (`usr/libexec/banana/*`), default configs/themes |
| `buildroot/` | **Buildroot external tree** | the defconfig, Banana packages (`package/*`), the SM4450 board support (`board/retroid-pocket-classic/*`) |
| `tools/` | **Dev tooling** | `qemu-boot.sh`, `import-rnds.sh`, the art generators |
| `docs/` | **Docs** | `ARCHITECTURE.md`, `RPC-PORT.md`, `RPC-TEST.md`, `ROADMAP.md`, `PERFORMANCE.md` |

Runtime architecture (from `docs/ARCHITECTURE.md`): **no systemd, no EmulationStation, no desktop, no initramfs, no
udev, no sound server; no compositor for the menu** (the menu draws straight on KMS, and a game runs under cage, a
kiosk Wayland compositor, so the panel's turn is applied once to everything). A read-only squashfs root + an exFAT
data partition; busybox init; two always-resident userland processes (`banana-ui`, `bananad`).

## 2. Boot & storage

1. **OpenABL** (arm-efi, in the handheld's `abl_a`/`abl_b` slots; the card carries no bootloader) reads the FAT
   partition's `extlinux/extlinux.conf` and starts the `DEFAULT` slot's kernel.
2. GPT: p1 `BANANA-BOOT` (FAT, `a/`+`b/` kernel+dtb slots), p2 `banana-root-a`, p3 `banana-root-b` (squashfs/LZ4),
   p4 `BANANA` (exFAT, made on the first boot).
3. Kernel `root=PARTLABEL=banana-root-{a,b} rootfstype=squashfs ro rootwait`; **no initramfs**.
4. `rcS` mounts pseudo-fs + data, loads the audio/fan modules, starts `bananad`, `banana-perf ui`, then
   `banana-ui`; Wi-Fi, the clock, the GPU and the rest go to background subshells.

**A/B updater:** `banana-<board>-<version>.update.tar` = kernel + dtb + `rootfs.squashfs` + hashes; `update apply`
verifies, writes the *inactive* slot, reads back, then rewrites `DEFAULT` (`Previous version` flips back).

## 3. Build configuration

| File | Purpose |
|---|---|
| `Makefile` | The board target `retroid-pocket-classic`; clones Buildroot `2026.02.3` to `.buildroot/`, `O=output/<board>`; `make ui`, `make test`, `make bench`, `make local-rebuild`, `make check-local-build` |
| `buildroot/configs/banana_retroid_pocket_classic_defconfig` | The one image (the RPC) |
| `buildroot/board/retroid-pocket-classic/` | DTS, `linux-rocknix.config` + patches, firmware, audio topology, `extlinux.conf.in`, `genimage.cfg.in`, post-build/post-image |
| `buildroot/package/*/*.mk` | Banana's packages (`banana-ui`, `bananad`, the libretro cores, RetroArch, PortMaster, libudev-zero) |
| `.github/workflows/ci.yml` | `checks` (every push: `make test`, the ALSA build, the sleep test, shell parse, defconfig-preservation) → `image` + `boot` (manual) |

**Toolchain/deps:** Bootlin external aarch64 glibc stable, `BR2_cortex_a76_a55`, `-O2`, `SSP_NONE`,
`RELRO_PARTIAL`, ccache 3G. Linux 7.2 (+ ROCKNIX SM4450 patches), SquashFS LZ4, host genimage/dosfstools/mtools.

## 4. Hardware integration

- **Display** (`ui/src/kms.cpp`): one MIPI-DSI panel at 1240×1080, `Display::openSingle()`, and `Display::turn()`
  for the device tree's `rotation = <90>` (a 4×4 register transpose, unit-tested in `ui/tests/turn-test.cpp`).
  Two KMS dumb buffers, page flips on vblank; damage-only copies into the uncached scanout buffer.
- **Input**: the MCU's `rsinput` gamepad (`ui/src/input.cpp` reads it, never grabs it; bananad reads it too) and
  the FT5452 touchscreen, routed by the sysfs path containing `BANANA_TOUCH`.
- **Audio**: no sound server. `bananad` publishes the route to `/run/banana/audio-route` and writes
  `/run/banana/asound.conf`; the AudioReach DSP (the `sc8280xp`/q6apm stack) takes the mixer route at each open,
  so banana-ui and `run-retroarch` re-assert it before opening the frontend. WCD937x + 2×AW88166.
- **GPU**: the Adreno a613 through Mesa freedreno (both the kernel catalog and Mesa need the 613 id).
- **Power**: `bananad` — a no-lid slate (its idle timer dims but does not suspend unless asked), the power key,
  the RTC battery check, LEDs off `power_supply` uevents, the fan left to the kernel's `pwm_fan`.
- **Clocks** (`banana-perf`): every `policy*` set per profile (`ui`, `powersave`, `balanced`, `performance`).

## 5. Entry points

| Concern | Entry file(s) |
|---|---|
| Boot/init | `system/rootfs/etc/init.d/rcS`, `etc/inittab` |
| Power daemon | `system/bananad/bananad.c` |
| Menu core | `ui/src/main.cpp` (main loop, launch/handover, iris), `library.cpp` (Source), `soft.cpp` (Backend), `kms.cpp` (Display), `turn.h` (rotation), `input.cpp`, `sound.cpp`, `settings.cpp`, `splash.cpp`, `wifi.cpp`, `carts.cpp`/`cart3d.cpp` |
| Imported engine | `ui/rnds/RndsUI.cpp`, `ui/rnds/RndsRaster.cpp` |
| Game contract | `system/rootfs/etc/banana/systems.conf` + `usr/libexec/banana/run-*` |
| Tests | `ui/tests/run.sh` (+ `fixture.sh`, `bench.sh`, `turn-test.cpp`), `system/bananad/tests/sleep-test.sh`, `tools/qemu-boot.sh` |

## 6. Testing surface

Strong off-device coverage: headless renders at both canvas sizes with the views and settings checked, the
rotation transpose unit-tested, the namespaced sleep test, the full QEMU boot test including the A/B update, and
the defconfig-preservation check. The gaps are the things QEMU cannot exercise: the panel's DSI timing, suspend,
sound, Wi-Fi and the GPU — `docs/RPC-TEST.md` is the list to check on the handheld.

## 7. Strengths

- Disciplined minimalism with the rationale documented at every layer (`docs/ARCHITECTURE.md`).
- A/B updates with end-to-end hash verification and rollback, exercised in CI/QEMU.
- A boot path with almost no userspace dependencies, and damage-diffed software compositing that idles in
  `poll()`.
- Hardware-independent test seams (`TestPower`, `BANANA_*` env, `--headless` script DSL) that make most of the
  system testable without a handheld.

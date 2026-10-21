# BananaLinux — Codebase Analysis Report

**Repository:** `JorreFog/BananaLinux` @ `819aabc` · **Version:** `0.4.1` · **Scope:** 319 tracked files (~16k LOC C/C++), no submodules.

> **Note on commonly assumed names.** Three named components do not exist in this tree; the closest real equivalents are listed so the report stays accurate:
>
> - **`BananaPuddingOS`** → does not exist. The home menu is **ROCKNIXDS Pixel**, driven by a Banana fork/import (`ui/rnds/`) plus Banana's own **Carts** theme; Banana's palette is applied over the theme (`tools/banana-palette.sh`).
> - **`GBARunner`** → does not exist. GBA runs under **RetroArch + gpSP** (or mGBA), via `run-retroarch`.
> - **`RK3568`** → the handhelds are **RK3566**-class (4× Cortex‑A55). The tree labels things `rk3568` because Rockchip shares `rk3568.dtsi`/`rk3566` silicon naming and the ported ROCKNIX support + BL31 are named for RK3568. Also: **no CMakeLists.txt exists in-repo**; the build is Buildroot + plain Makefiles. BananaDS is CMake, but it lives in the external `SuperDrastic` repo.

---

## 1. Architecture & Project Layout

### 1.1 Subsystem map

| Path | Subsystem | Role |
|---|---|---|
| `ui/` | **Frontend / home menu** | `banana-ui` (C++17): KMS host (`src/`), imported render engine (`rnds/`), assets, headless test |
| `system/bananad/` | **Power daemon** | Sleep/lid/power/LEDs/sound-routing/brightness/battery (`bananad.c`), panel retune (`banana-panel-tune.c`) |
| `system/rootfs/` | **Init + rootfs overlay** | `init.d/rcS`, `inittab`, `rcK`, `banana-perf`, launchers (`usr/libexec/banana/*`), default configs/themes |
| `buildroot/` | **Buildroot external tree** | 3 defconfigs, Banana packages (`package/*`), RG DS board support (`board/rgds/*`) |
| `tools/` | **Dev tooling** | `import-rnds.sh`, `sync-rocknix-hw.sh`, `qemu-boot.sh`, art generators, palette script |
| `web/` | **Website** | GitHub Pages (`index.html`, `save-mover/`), no build step |
| `docs/` | **Docs** | `ARCHITECTURE.md`, `EMULATORS.md`, `FIRST-BOOT.md`, `ROADMAP.md` |

Runtime architecture (from `docs/ARCHITECTURE.md`): **no systemd, no compositor, no EmulationStation, no desktop, no initramfs, no udev, no sound server.** Read-only squashfs root + exFAT data partition; busybox init; two always-resident userland processes (`banana-ui`, `bananad`).

### 1.2 Boot & storage flow

1. `sector 64` → `u-boot-rockchip.bin` (DDR init + SPL + TF‑A BL31 + U-Boot), no boot delay.
2. GPT: `p1 BANANA-BOOT` (FAT, `a/` + `b/` kernel+dtb slots, `extlinux/extlinux.conf` `DEFAULT`), `p2 banana-root-a`, `p3 banana-root-b` (squashfs/LZ4), `p4 BANANA` (exFAT, created on first boot).
3. Kernel `root=PARTLABEL=banana-root-{a,b} rootfstype=squashfs ro rootwait`; **no initramfs**.
4. `rcS` mounts pseudo-fs + data, starts `bananad`, `banana-perf ui`, then `banana-ui`; Wi‑Fi/clock/GPU/SSH/share/panel-tune run in background subshells.

**A/B updater:** `banana-<board>-<version>.update.tar` = kernel + dtb + `rootfs.squashfs` + hashes; `update apply` verifies, writes the *inactive* slot, reads back, then rewrites `DEFAULT` (`Previous version` flips back). Proven in QEMU.

### 1.3 Build configuration

| File | Purpose |
|---|---|
| `Makefile` | Board targets `rgds`, `rgds-plus`, `universal`; clones Buildroot `2026.02.3` to `.buildroot/`, per-board `O=output/<board>`; `make ui`, `make test`, `make clean` |
| `buildroot/configs/banana_{rgds,rgds_plus,universal}_defconfig` | The three images |
| `buildroot/external.mk` | Auto-includes `package/*/*.mk`; SDL2 video-dummy/offscreen; applies ROCKNIX kernel patches (fuzz), copies panel/joypad drivers, copies DTS + dts-patches |
| `buildroot/package/*/*.mk` | 24 packages (see §3.2) |
| `buildroot/board/rgds/post-build.sh`, `post-image.sh`, `genimage.cfg.in`, `extlinux.conf.in` | Rootfs trim, image assembly, boot slots |
| `.github/workflows/ci.yml` | `checks` (every push/PR) → `image` + `boot` (manual dispatch `image=true`) |
| `.github/workflows/pages.yml` | Deploys `web/` to Pages on `main` |

**Toolchain/deps:** Bootlin external `aarch64 glibc stable`, `BR2_cortex_a55`, `-O2`, `SSP_NONE`, `RELRO_PARTIAL`, ccache 3G. Linux `v7.2.7` custom tarball (mainline mirror + ROCKNIX patches), U-Boot `v2026.01` (`quartz64-a-rk3566` defconfig), SquashFS LZ4, host `genimage`/`dosfstools`/`mtools`.

---

## 2. Hardware Integration & Drivers

### 2.1 Dual-screen display

- **Panels:** two MIPI‑DSI panels `dsi0`/`dsi1` → VOP `vp0`/`vp1`, driven by the ROCKNIX **generic DSI panel driver** `panel-generic-dsi.c`, described as **text timing strings** (`"M clock=62770 horizontal=… vertical=…"`) in the DTS. PWM backlights `backlight0`/`backlight1`, per-panel reset/rails. 640×480 (RG DS) / 1024×768 (Plus).
- **`banana-ui` display path (`ui/src/kms.cpp`):** opens `/dev/dri/card*`, finds **two connected connectors of the top panel's type** (`DSI-2`, or `Virtual-*` in QEMU), assigns non-overlapping CRTCs, forces matching modes, allocates **two dumb buffers per panel**, page-flips via `drmModePageFlip(..., DRM_MODE_PAGE_FLIP_EVENT, …)` with a pending counter and `drmHandleEvent` IRQ loop. Composes in cached memory, copies only damaged rectangles into uncached scanout.
- **VBlank pacing (`ui/src/main.cpp`):** next frame drawn while flips are pending; `quietUntil` throttles “changed nothing” frames to one refresh. A not-yet-applied fix for VOP2 port start-up lives in `buildroot/pending-patches/linux/0001-drm-rockchip-vop2-start-video-ports-in-step.patch`.
- **Refresh tuning:** DTS `dts-patches/0001-rg-ds-panels-at-60hz.patch` gives the RG DS 60.0013 Hz; **`banana-panel-tune.c`** reads each VOP's *real* clock from debugfs `clk_summary` and rewrites front porches in-place in the DTB (digit-count-preserving) to reach ≤0.002 Hz of 60 — used for the RG DS Plus.
- **Two-panel handover strategies for emulators:**
  - `Display::lease()` → `drmModeCreateLease` gives RetroArch **one panel only**; `run-retroarch` preloads `banana-leaseshim.so` → RetroArch monitor 1 = leased connector; the menu keeps the other panel and renders its game page + achievements, converting touch to `SAVE_STATE`/`LOAD_STATE`/`MENU_TOGGLE`/`QUIT` commands over `stdin_cmd_enable` FIFOs.
  - SuperDrastic/BananaDS take DRM master and draw each DS screen to its own panel (`frontend=kms`, atomic commit from an `SCHED_FIFO` presenter).

### 2.2 Touch & buttons

- **DTS:** `goodix,gt911` on `i2c5` (bottom panel; `touchscreen-size-x/y = 1024/768`), plus the ROCKNIX **`rocknix-singleadc-joypad`** (ADC mux) exposing d-pad, A=east/B=south, X/Y, L/R/L2/R2, Start/Select, F=`BTN_MODE`, thumbs; hall lid on `SW_LID`; volume keys on `gpio-keys` with autorepeat.
- **`ui/src/input.cpp`:** enumerates evdev, classifies multifinger vs single-touch/absolute-pointer, scales raw ABS ranges to panel pixels, and routes the **bottom touchscreen by sysfs realpath containing `BANANA_TOUCH`** (default `fe5e0000.i2c`). inotify hotplug rescan. Never grabs; reads alongside bananad and games.
- **bananad** reads the *same* evdev nodes but masks to `EV_SYN|EV_KEY|EV_SW` (so touch/stick motion during a game doesn't spin the daemon) and loads USB drivers from uevents' `MODALIAS` **as a udev replacement**.

### 2.3 Audio (no sound server, no music mixer)

There is **no background-music mixer or ALSA mixing daemon.** “Mixer” throughout the tree means `amixer` control names. The audio design:

- **Routing (`bananad.c::audio_route`):** writes ALSA's default device to `/run/banana/asound.conf` (`/etc/asound.conf` points there). RG DS: `rk817` (card 0) `Playback Mux = HP` feeds jack+speaker amp, with `Internal Speakers Switch` off when headphones are in. RG DS Plus: playback on the **aw88166** amps (`Left/Right PCM`) or `rk817` when headphone switch is on; **capture always `rk817`** (mic). The aw88166 is forced to **48 kHz** (measured 9% pitch-up otherwise), and the rk817's playback and capture share one I2S DAI that `rockchip-i2s-tdm` marks **`symmetric_rate`**, so `audio_route` pins both directions of `default` to S16 stereo 48 kHz (ALSA's `plug` resamples): the DS emulators play at 48 kHz but opened the mic at 44.1 kHz, which the DAI refused, so the microphone never started. Route changes are picked up by a reload-on-open hook; RetroArch is sent `AUDIO_REINIT` (patched). `audio_setup` also sets the rk817's capture gains (`Mic Capture Gain`, `Master Capture Volume`) at boot, after `alsactl restore`, so the DS microphone can't be left muted; the DS launchers turn the mic on unless the card's settings disable it (`run-drastic`'s `DSHOOK_MIC_THRESH`, `run-bananads`'s `mic.source`).
- **Menu sound (`ui/src/sound.cpp`):** a single thread keeps one `snd_pcm` open, plays 16-bit WAV clips in 10 ms chunks with silence fills (also keeps the aw88166 amp clock alive), watches `/run/banana/asound.conf` inode for jack changes, and can `release()` the device before a game launches.
- **Volume/brightness:** `bananad` runs `/usr/libexec/banana/volume` (handles the aw88166's count-*down* `Left/Right PCM`) and `F + volume` for both backlights; remembers state in `/data/banana/backlight` and `asound.state`.

### 2.4 VBlank / framebuffer sync & sleep/power

- Damage-diffed software compositor (`ui/src/soft.cpp`) with per-screen draw-list diffing and clipped redraws; only changed columns `memcpy`'d to the uncached buffer.
- **Sleep (`bananad.c::suspend_now`):** `SIGSTOP` game process group → `sleep.d/* pre` hooks (Wi‑Fi down, RTL8821CS must unload) → `sync()` (`SUSPEND_SKIP_SYNC`) → RTC `wakealarm` (battery check, default 4 h) → `/sys/power/mem_sleep=deep` + `mem`. On wake: game `SIGCONT`, `banana-ui` `SIGUSR1` redraw **without modeset**, then background `post` hooks and LED/headphone reconciliation. Fallback ladder: per-device then `s2idle`. Charging-with-lid-closed avoids suspend (PWM charging LED would drop).
- **LEDs** driven by kernel triggers (`<battery>-charging`, timer blink), bananad only reacts to `power_supply` uevents + a 1/min poll.

### 2.5 Custom patches, forks & components

| Item | Where | What |
|---|---|---|
| ROCKNIX hardware sync | `tools/sync-rocknix-hw.sh` | Pinned ROCKNIX commit; copies kernel patches, DTS, config, DSI/joypad drivers, U-Boot patches, BL31 |
| Kernel patches | `board/rgds/linux-patches/` (~25) | ASoC aw87391/aw88166, rk817 charger/gauge, MMC SD resume retry, ADC keycode redirect, CMA region, RK356x idle states, Goodix fixes, input-polldev |
| U-Boot patches | `uboot-patches/`, `uboot-patches-universal/` | boot-order, SARADC board-id detection (universal; **not booting yet**, ROADMAP) |
| Boot animation | `ui/src/splash.cpp` + `ui/assets/splash/*` | Banana drop + BANANAOS typing + cartridge insertion, then an ordered-dither **iris** transition to the menu |
| Themes | `system/rootfs/usr/share/banana/themes/*`, `tools/banana-palette.sh`, `web` theme-builder | ROCKNIXDS Pixel dark/light, Carts (Inter), user JSON themes |
| "Quick menu" overlays | `ui/src/settings.cpp` (+ `be.overlay`) | In-menu Settings groups, Wi‑Fi keyboard, first-time setup, low-battery notice, volume/brightness OSD pills, in-game DS menu (L3) |
| GBA etc. | `buildroot/package/libretro-*`, `run-retroarch` | **No GBARunner** — RetroArch + gpSP (mGBA optional), cores for GB/GBC/NES/SNES/MD/PSX/N64/fake-08 |
| DS emulation | `buildroot/package/{superdrastic,drastic,bananads}`, `run-*` | SuperDrastic (DraStic) and BananaDS (melonDS core, KMS frontend, own governor) |

---

## 3. Output Summary

### 3.1 Key source entry points

| Concern | Entry file(s) |
|---|---|
| Kernel board support | `buildroot/board/rgds/dts/rk3568-anbernic-rg-ds{,-plus}.dts`, `kernel/panel-generic-dsi.c`, `kernel/rocknix-singleadc-joypad.c` |
| Kernel/U-Boot config | `linux-rocknix.config` + `linux-banana.fragment`, `uboot-banana.fragment` |
| Boot/init | `system/rootfs/etc/init.d/rcS`, `etc/inittab`, `etc/init.d/rcK` |
| Power daemon | `system/bananad/bananad.c`, `banana-panel-tune.c` |
| Menu core | `ui/src/main.cpp` (main loop, launch/handover, iris), `library.cpp` (Source), `soft.cpp` (Backend), `kms.cpp` (Display), `input.cpp`, `sound.cpp`, `settings.cpp`, `splash.cpp`, `wifi.cpp`, `scrape.cpp`, `carts.cpp`/`cart3d.cpp` |
| Imported engine | `ui/rnds/RndsUI.cpp`, `ui/rnds/RndsRaster.cpp` |
| Game contract | `system/rootfs/etc/banana/systems.conf` + `usr/libexec/banana/run-*` |
| Tests | `ui/tests/run.sh` (+ `fixture.sh`, `fake-wifi.sh`), `system/bananad/tests/sleep-test.sh`, `tools/qemu-boot.sh` |

### 3.2 Build dependencies (runtime packages selected by `*_defconfig`)

`banana-ui`, `bananad`, `bananads`, `superdrastic`, `drastic`, `retroarch` (+ `libretro-*` cores, GLSL shaders, `libretro-cheats-nds`), `mesa3d-panfrost` (Mesa 25.0.7, no LLVM), `sdl2` (KMSDRM/GLES + dummy/offscreen), `alsa-lib/utils`, `freetype`, `libdrm`, `zlib`, `libcurl`, `exfatprogs`, `util-linux`, `wpa_supplicant` (WPA3, cli, autoscan), `ksmbd-tools`, `dropbear`, `ca-certificates`, `bash`, Python3, SDL add-ons, `openal`, vorbis/theora/mpg123, `libpng`/`jpeg` (PortMaster); `libudev-zero`; `linux-firmware` rtw88 + `rocknix-extra-firmware` + `wireless-regdb`.

### 3.3 Areas for optimisation / missing hooks (grounded in code + ROADMAP)

1. **Menu is CPU-bound on 1024×768.** `soft.cpp` renders *both* panels on one core; README/ROADMAP call it “slow” on the Plus. Candidate: GPU/Panfrost composition or splitting the two panels across cores.
2. **`universal` image not shipped.** Built but excluded from CI; SARADC board-id U-Boot branch doesn't boot the RG DS (clang build) — `uboot-patches-universal/`, ROADMAP.
3. **VOP2 vblank patch unapplied** (`buildroot/pending-patches/linux/…vop2-start-video-ports-in-step.patch`) — a known dangling hook.
4. **Bluetooth absent** though DTS wires `rtl8821cs-bt`; kernel fragment leaves BT out.
5. **ScreenScraper not actually used** — account is stored, but the working scraper (`scrape.cpp`) only pulls libretro thumbnails/metadat and *silently skips* unmatched names; README's “no scraper yet” is stale.
6. **Live audio route change gap:** most players only re-read `/run/banana/asound.conf` on next open (RetroArch patched with `AUDIO_REINIT`; documented known gap).
7. **Wi‑Fi WEP/802.1X require manual `wifi.conf` editing.**
8. **BananaDS not the default** until validated on hardware; its governor/power tuning A/B (`power.cpu_latency_us`) pending.
9. **Panel tuning heuristic** (digit-count-preserving front-porch search) is best-effort and risks odd timings; only rewrites when a gain ≥0.005 Hz.
10. **Testing surface:** strong off-device coverage (headless renders at both sizes, namespaced sleep test, full QEMU 5-phase boot incl. A/B update, defconfig-preservation check) — but **no on-device CI**, and QEMU can't exercise DSI timing, suspend, sound, Wi‑Fi or the GPU.
11. **Version skew:** kernel tarball is `v7.2.7`, U-Boot `2026.01`, Buildroot `2026.02.3` — keep an eye on ROCKNIX patch fuzz (applied with `-p1` fuzz by `external.mk`).
12. **Minor dead ends:** `auto-sleep`/`dim` disabled on charger; `usb_host0_ohci` disabled “due to PM issues”; `cw2015` gauge disabled.

### 3.4 Notable strengths

- Extremely disciplined minimalism and documented rationale at every layer (see `docs/ARCHITECTURE.md`).
- A/B updates with end-to-end hash verification and rollback, exercised in CI/QEMU.
- Boot-to-menu path with almost no userspace dependencies; damage-diffed software compositing that idles in `poll()`.
- Hardware-independent test seams (`TestPower`, `BANANAD_TEST_LID`, `BANANA_*` env, `--headless` script DSL) that make most of the system testable without a handheld.

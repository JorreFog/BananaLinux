# Building the AYN Thor's SD card image

This is the hand-off for whoever builds the Thor's image, a person or an AI on their own machine. There are two
images in this repository:

- **A. BananaOS's own image** (Buildroot, `docs/THOR-OS.md`): the menu on the panels with no compositor and no
  systemd under it, its own emulator packages, PC games, the browser. This is the one to build.
- **B. ROCKNIX's image with BananaOS's menu as its frontend** (`docs/THOR-ROCKNIX.md`): everything ROCKNIX ships,
  with more running under the menu. Its steps are at the end.

Neither has been built end to end or booted. The first build will need fixes, and they belong in this repository.

## A. BananaOS's own image

### What you need

- Linux x86_64, about 120 GB of free disk and 16 GB of RAM or more (Qt 6, WebKit, Mesa, Dolphin and the kernel are
  in it; WebKit and Dolphin want 1.5-2 GB per compiler).
- Buildroot's host packages: `build-essential git rsync cpio unzip bc file wget python3 libssl-dev libncurses-dev`
  (Debian's names). Buildroot says what else it misses when it starts.
- Network for the whole build: sources are downloaded as they are needed, and xemu's build fetches its Meson
  subprojects while it configures.
- Many hours for the first build (a day on a small machine). Later builds reuse `.buildroot/dl` and the output
  folder.

### Steps

```sh
git clone https://github.com/JorreFog/BananaLinux
cd BananaLinux
git checkout claude/pensive-rubin-cy7ro4     # or main once it's merged
make thor
```

`make thor` clones Buildroot 2026.02.3 into `.buildroot`, applies `buildroot/buildroot-patches` to it (a newer Mesa
with Turnip), loads `buildroot/configs/banana_thor_defconfig` into `output/thor` and builds. The image is
`output/thor/images/banana-thor.img.gz`, with the update package beside it.

One package at a time: `make <package>-thor` (`make libretro-fbneo-thor`), and Buildroot's own targets the same way
(`make dolphin-dirclean-thor`, `make linux-menuconfig-thor`, `make menuconfig-thor`).

**An output folder from before the tuning commit must be thrown away** (`rm -rf output/thor`): the compiler's CPU
setting changed (SVE off, `buildroot/external.mk`), and Buildroot doesn't notice that by itself. Programs built with
the old setting would crash on the Thor.

### What has been compiled already, and what hasn't

Checked on a PC, with this repository's configuration and Buildroot's cross compiler for the Thor:

- the configuration loads and keeps every option; every package's dependencies resolve;
- Box64 (the PC games' x86 translator) builds;
- the new RetroArch cores: see the table in `docs/THOR-EMULATORS.md` ("Build state"), which says for each one
  whether it was compiled;
- the menu (`banana-ui`) builds for a PC and passes its tests there; it has not been cross-built with the Thor's
  toolchain in this configuration.

Not compiled at all: the kernel with ROCKNIX's patches in this tree, Mesa with the Turnip patches for ARM64, Qt 6,
WPE WebKit, sway and wlroots, PipeWire, and the eight standalone emulators (Dolphin, ARMSX2, Azahar, PPSSPP,
Vita3K, xemu, Flycast, ScummVM).

### When a package fails to build

Expect it. The packages are ordered here by how likely they are to need work:

| Package | Where | Likely trouble |
|---|---|---|
| `armsx2` | `buildroot/package/armsx2` | upstream builds with clang; this recipe uses GCC and its SDL 3 front end instead of Qt |
| `azahar` | `buildroot/package/azahar` | Qt 6.9 from Buildroot instead of its bundled Qt; Wayland instead of X11 |
| `dolphin`, `vita3k` | `buildroot/package/...` | their bundled libraries cross-compiled; 17 ROCKNIX patches on Dolphin |
| `xemu` | `buildroot/package/xemu` | Meson downloads subprojects while configuring; system SDL 3, no GTK |
| `mesa3d` | `buildroot/patches/mesa3d` | five Turnip patches (4,000 lines) that apply cleanly but were never built for ARM64 |
| `sway-lite`, `wvkbd` | `buildroot/package/...` | written here (Buildroot's sway needs systemd); built on a PC only as far as their dependencies resolving |
| `linux` | `buildroot/board/thor` | ROCKNIX's patch series on 7.2 with two config fragments on top |
| the cores | `buildroot/package/libretro-*` | a compiler flag one core's makefile doesn't take |

How to deal with one:

1. Read the end of the log (`output/thor/build/<package>-<version>/` has the tree; Buildroot prints the failing
   command). Fix the recipe in `buildroot/package/<package>/` and build again with
   `make <package>-dirclean-thor && make <package>-thor`.
2. A package that can't be made to work quickly can be left out so the rest of the image builds. Each emulator has
   its own switch (`buildroot/package/thor-emulators/Config.in` only turns them on by default): set it off with
   `make menuconfig-thor`, or put `# BR2_PACKAGE_<NAME> is not set` in the defconfig (`# BR2_PACKAGE_ARMSX2 is not
   set`, `# BR2_PACKAGE_LIBRETRO_FBNEO is not set`).
   The shelf for a system whose emulator is missing shows its games and fails to start them; take its section
   out of `buildroot/board/thor/systems.conf` if the image is to be handed on that way.
3. No `.hash` files come with the new packages (their archives couldn't be downloaded where they were written), so
   Buildroot warns that a download isn't checked and goes on. Adding them is a good first contribution: Buildroot
   prints the hash it saw.

Before pushing a fix: `make test`, and the scripts' own checks (`for t in system/tests/*-test.sh; do sh $t; done`,
`sh rocknix/tests/run.sh`). `.github/workflows/ci.yml` runs the same.

### Putting it on the Thor

1. Write `banana-thor.img.gz` to a microSD card (Raspberry Pi Imager, balenaEtcher, or
   `gunzip -c banana-thor.img.gz | sudo dd of=/dev/sdX bs=4M conv=fsync`).
2. Once per handheld, the Thor needs OpenABL in its `abl_a`/`abl_b` partitions so it can boot from the card:
   `buildroot/board/thor/openabl-README.txt` and `docs/THOR-PORT.md`. **Back up the stock ABL first.** Nothing else
   on the Thor's internal storage is touched: Android stays, and without the card the Thor boots it.
3. Card in, power on. The first boot makes the data partition on the rest of the card, then the first-time setup comes
   up (language, Wi-Fi, time zone, the Thor's name, RetroAchievements, home server, Steam, the fan and the lights,
   box art).

### What to look at on the first boot

Nothing here has run on a Thor, so the first boot is a bring-up. The `thor-bringup` skill
(`.claude/skills/thor-bringup`) turns what the handheld reports into fixes; `docs/THOR-PORT.md` has the milestones.
The quickest way in is the USB cable: this build has USB debugging on (`docs/THOR-OS.md`), so a computer plugged
into the Thor gets a network card (`ssh root@10.55.0.1`, no password), a serial console and adb. Failing that, SSH
over Wi-Fi, or a USB keyboard and `console=tty0`:

- `/usr/libexec/banana/hw-probe` prints what the kernel found (panels, touchscreens, the pad, sound cards, the fan
  and the lights), to set against `etc/banana/hw/thor.conf`;
- the menu's log is `/data/banana/logs/banana-ui.log`;
- `docs/THOR-OS.md` ends with the list of what was never run, in the order to try it; `docs/THOR-TUNING.md` has the
  command that measures each tuning.

Two switches for a first boot that goes wrong, both in `banana/settings.conf` on the card's data partition (it is
exFAT: any computer can edit it): `home_switch = off` gives a game the display the plain way if a standalone
emulator or a PC game doesn't start, and `theme = dark` goes back to the RG DS's look if the Thor's theme draws
wrong.

## B. ROCKNIX's image with BananaOS's menu

### What you need

- Linux x86_64 with Docker (ROCKNIX builds in its own build container), about 150 GB free disk and plenty of RAM
  (16 GB or more; some packages, MAME's among them, take 1-2 GB per compiler).
- Several hours for the first build. Later builds reuse ROCKNIX's `sources/` downloads and its build folders.
- git and python3 on the host.

### Steps

1. This repository at the Thor branch (or `main` once it's merged):

   ```sh
   git clone https://github.com/JorreFog/BananaLinux
   cd BananaLinux
   git checkout claude/pensive-rubin-cy7ro4
   ```

2. ROCKNIX at the commit this work was made against (`buildroot/board/thor/ROCKNIX`'s `commit=`):

   ```sh
   git clone https://github.com/ROCKNIX/distribution ../rocknix
   git -C ../rocknix checkout "$(sed -n 's/^commit=//p' buildroot/board/thor/ROCKNIX)"
   ```

3. BananaOS into it. This copies the frontend package and Banana's emulator and app packages into the checkout,
   adds Banana's RetroArch patches, and lists everything where ROCKNIX's build looks (`ADDITIONAL_PACKAGES`, the
   emulator list, `es_features.cfg`). Running it again is safe.

   ```sh
   sh tools/rocknix-banana.sh ../rocknix SM8550
   ```

4. ROCKNIX's own build:

   ```sh
   cd ../rocknix
   make docker-SM8550
   ```

   The image lands in `release/` (`*.img.gz`), with an update tar beside it.

CI does steps 2-4 too: `.github/workflows/ci.yml`'s `rocknix-thor` job, started by hand (Actions > CI > Run workflow,
with "thor_image" ticked). It puts the image up as an artifact.

### When a package fails to build

The packages most likely to need work on a first build, and where they live here:

| Package | Here | Likely trouble |
|---|---|---|
| `banana-frontend` | `rocknix/packages/ui/banana-frontend` | dependency names (`SDL2`, `curl`, `openssl`, `libssh2`), the cross `pkg-config`, banana-fetch's libraries |
| `bananads-sa` | `rocknix/packages/emulators/standalone/bananads-sa` | the melonDS source path from `melonds-sa` (`get_pkg_version`), `PKG_CMAKE_SCRIPT`, the binary's path in the build folder |
| `bananafront-sa` | `.../standalone/bananafront-sa` | `make sdl`'s flags (patch 0001) |
| `mame-gnw` | `.../standalone/mame-gnw` | MAME's cross build (`OVERRIDE_CC`, `SDL_INSTALL_ROOT`, the host python), BananaOS's patches applying to it |
| `banana-apps` | `rocknix/packages/apps/banana-apps` | the Store's patches (0001-0003) and the two-window one (0010) applying in order |
| `bananagba-lr`, `banananes-lr`, `bananasnes-lr` | `rocknix/packages/emulators/libretro` | their own makefiles' flags |

Fix the package here, run `tools/rocknix-banana.sh` again, and rebuild just that package in ROCKNIX's way (`make
docker-SM8550` again, or ROCKNIX's `scripts/build <package>` inside its container). A package that can't be made to
work on the first try can be left out by taking it off the list in `tools/rocknix-register.py` (`EMULATORS`,
`PACKAGES`): the menu and ROCKNIX's own emulators don't need Banana's.

Before pushing a fix, run this repository's checks: `make test` (or at least `sh rocknix/tests/run.sh`).

### Putting it on the Thor

1. Write the image to a microSD card (Raspberry Pi Imager, balenaEtcher or `dd`).
2. Once per handheld, the Thor needs OpenABL in its `abl_a`/`abl_b` partitions so it can boot from the card. Follow
   ROCKNIX's Thor page (https://rocknix.org/devices/ayn/thor-sm8550). **Back up the stock ABL first.** Nothing else on
   the Thor's internal storage is touched; Android stays, and without the card the Thor boots it.
3. Card in, power on. The first boot is ROCKNIX's (it makes its storage partition), then Banana's menu comes up on
   both screens. `banana.frontend=0` in ROCKNIX's settings goes back to EmulationStation.

### What to look at on the first boot

`docs/THOR-ROCKNIX.md`'s "What only the Thor can tell", and the `thor-bringup` skill (`.claude/skills/thor-bringup`),
which turns what the handheld reports into fixes. The menu's log is
`/storage/.config/banana-data/banana/logs/banana-ui.log`; its service is `bananasway` (`journalctl -u bananasway`).
Known gap: the boot animation has no artwork for the Thor's screen shapes yet, so the menu starts without it.

# BananaOS's menu on ROCKNIX: the AYN Thor

One of the two routes to the AYN Thor in this repository: ROCKNIX's SM8550 image with BananaOS's dual-screen menu as
its frontend. It is not BananaOS's own system on the Thor. The other route is BananaOS's own Buildroot image
(`make thor`: `docs/THOR-OS.md`, `docs/BUILD-THOR-IMAGE.md`, `docs/THOR-PORT.md`), which is the one being brought up
now; this one stays as the alternative. The hardware findings hold either way.

## Why

ROCKNIX already runs all of the Thor:

- the kernel with its 71 patches
- OpenABL boot
- sway on both screens
- PipeWire, the WCN7850, the fan, the stick LEDs, the panel calibration
- every emulator, set up per device
- updates

What it lacks is a menu built for two screens. Today the top screen gets EmulationStation and the bottom one gets
lowerdeck, a small Python/SDL touch app that drives RetroArch over its network commands. BananaOS's menu was built for
two screens from the start. So the Thor gets ROCKNIX's system and BananaOS's menu, and every hardware fix ROCKNIX
makes for SM8550 reaches it with no porting.

## How it fits

| Piece | ROCKNIX's | With Banana |
|---|---|---|
| Display | sway, both outputs (`111-sway-init`: the internal panel's transform, the mode) | unchanged |
| Frontend | `essway.service`: `start_es.sh`, EmulationStation on the top output; lowerdeck on the bottom | `bananasway.service`: `start_banana.sh`, banana-ui as two fullscreen windows, one per output. essway is masked for the boot |
| Systems | `es_systems.cfg`, generated at build (`distributions/ROCKNIX/config/functions`: `add_es_system`) | read at every start by `es-systems-to-banana.py`: the same systems, folders, extensions and default emulator/core |
| Starting a game | `runemu.sh %ROM% -P%SYSTEM% --core=… --emulator=… --controllers=…` | the same command, with the system's default emulator and core |
| Input | evdev via libinput (sway), SDL's for the games | banana-ui reads evdev itself, as on the RG DS (sway doesn't grab the devices) |
| Data | `/storage` | the menu's in `/storage/.config/banana-data/banana` (`/data/banana`), games in `/storage/roms` |

In code:

- **`ui/src/kms_sdl.cpp`**: the menu's screens as a compositor's windows (SDL2 on Wayland), beside the KMS path.
  - Built with `make -C ui SDL=1`.
  - Chosen at run time when `WAYLAND_DISPLAY` is set, or with `BANANA_DISPLAY=sdl`.
  - The top window goes on the output named like `BANANA_TOP` (sway's `WLR_CON`), else on the widest output.
  - The menu's 4:3 canvas is fitted to each window (`panelfit.h`). sway has already turned the outputs upright.
  - SDL's events are pumped every 50 ms, so sway's pings are answered while the menu is idle.
  - A game is its own client: the menu closes its windows for it and reopens them after.
- **`rocknix/packages/ui/banana-frontend`**: the ROCKNIX package (`package.mk`, the systemd unit, `start_banana.sh`,
  the converter).
- **`tools/rocknix-banana.sh <ROCKNIX checkout> [SM8550]`**: copies the package and banana-ui's source into a ROCKNIX
  checkout and adds `banana-frontend` to the device's `ADDITIONAL_PACKAGES`. Then build with ROCKNIX's own
  `make docker-SM8550`.
- **`rocknix/ROCKNIX`**: the ROCKNIX commit this route is made and checked against, `4fc79d7c` (ROCKNIX's `next` on
  2026-11-13). The installer says so when a checkout is at another commit. (The Buildroot image's kernel has its own
  pin, `buildroot/board/thor/ROCKNIX`, moved only with its kernel patches: `.claude/skills/rocknix-sync`.)

Checked without a Thor:

- **`ui/tests/sway-test.sh`** runs sway headless with two outputs the Thor's size (1920x1080, 1240x1080). It starts
  the menu and screenshots both outputs. The top screen lands on the 1920x1080 output and the bottom on the 1240x1080
  one, each fitted with black bars.
- **`rocknix/tests/run.sh`** checks the converter against a sample in ROCKNIX's `es_systems.cfg` shape. A headless
  banana-ui read the converted list and launched a game from it.
- **The installer** ran on a ROCKNIX checkout at `4fc79d7c` (the pin) and is idempotent; `rocknix/tests/thor.sh` runs
  it on a made-up checkout as well.

**Not yet:** a ROCKNIX image build with the package, and a run on a Thor.

## What the image has, against BananaOS's own Thor image

`docs/THOR-OS.md` describes BananaOS's own Thor image (Buildroot), the reference for features. Section by section,
what this ROCKNIX image has of it: **ROCKNIX's** (ROCKNIX already does it, the menu drives it), **added** (in this
package now), **not on ROCKNIX** (with the reason).

### The board and the hardware profile

One ROCKNIX SM8550 image serves the AYN Thor, the Odin 2s and the AYANEO Pockets, while BananaOS's board and hardware
profile are per handheld. So they are written at boot and at the menu's start by `rocknix-device`, and
`/etc/banana/board` and `/etc/banana/hw.conf` are links into `/run/banana`:

- **On a Thor** (device tree model `AYN Thor`) the board is `thor`. The menu then does everything it does for the
  Thor on BananaOS's own image with no change to the menu: the Thor OLED theme by default, "Thor" as the handheld's
  name, the first-time setup's Controls, Home server and Steam slides, Steam and the PC games hub. The profile is
  `hw.conf` (ROCKNIX's services mode, `HW_SERVICES=systemd`, which every BananaOS script follows) with
  `hw-thor.conf`'s lines after it (the Thor button, the fan and lights, USB debugging, tune's parts).
- **On another SM8550 handheld** the board is `rocknix` and the profile is `hw.conf` alone, as before.

### Section by section

| THOR-OS.md | Here | How |
|---|---|---|
| Boot: OpenABL, the card's boot partition | ROCKNIX's | ROCKNIX's OpenABL route, kernel and squashfs |
| Init: busybox init, `rcS`, `bananad` | ROCKNIX's | systemd and ROCKNIX's autostart; `099-banana-frontend` (autostart) makes the menu the frontend, writes the board and profile, runs `tune boot` and `usb-debug up`. Keys, lid, sleep and brightness hotkeys are ROCKNIX's (input_sense, fake suspend) |
| The menu on both panels through KMS | added before | two SDL windows under ROCKNIX's sway (`kms_sdl.cpp`); `start_banana.sh` now powers the second output on and lets its touchscreen through (ROCKNIX's Thor sway config starts both off, for EmulationStation's bottom app) |
| Default theme Thor OLED; the other themes | added | the themes (`usr/share/banana/themes`: Thor OLED and its White, Clear Purple and Rainbow, and the others) are in the package; board `thor` makes Thor OLED the default |
| Games on KMS through a DRM lease, the game panel | ROCKNIX's / added before | ROCKNIX's RetroArch under sway, the game panel through RetroArch's network commands (`ra-netcmd`) |
| `run-wayland` for windowed programs | not needed | sway is always up on ROCKNIX; ROCKNIX's own rule moves second-screen windows to the bottom output |
| Memory: no compositor, no systemd at idle | not on ROCKNIX | ROCKNIX's systemd, sway and services always run under the menu; that is this route's cost |
| zram, sysctls, idle state | ROCKNIX's | `rocknix-memory-manager` (zram with LZ4 and zstd recompression, watermarks, overcommit) and its `096-cpuidle` quirk; `tune boot` adds only what ROCKNIX leaves alone: MGLRU's 1 s protection, huge pages never compacted in a fault, the card's scheduler and read-ahead |
| Emulators (RetroArch's 49 cores, Dolphin, ARMSX2, Azahar, PPSSPP, Vita3K, Flycast, ScummVM) | ROCKNIX's | ROCKNIX's SM8550 set, which is larger (Cemu, RPCS3, xemu, ares, gopher64...), plus Banana's own (BananaDS, BananaFront, mame-gnw, the BananaGBA/NES/SNES cores) |
| PC games: Box64 + Wine + DXVK + VKD3D-Proton, the hub | added | `pc-games`, `run-pc` and the hub (`[pc]`): ROCKNIX's Box64 (`/usr/bin/box64`, its box32 and binfmt entries); Wine 11.0 WoW64, DXVK 3.1.1 and VKD3D-Proton 3.0.1 fetched by "Set up PC games" as on BananaOS; the runtime and prefix in a plain folder (`/data/pc/runtime`: `/storage` is ext4, so no image file); games run in ROCKNIX's sway (no compositor of their own) and in ROCKNIX's `games.slice`. The games are in `roms/windows` (ROCKNIX's `roms/pc` is DOS's, now the menu's "dos" shelf) |
| ROCKNIX's own Windows and Steam | ROCKNIX's | its Windows shelf (scripts in `roms/windows` that call its Wine) and its Steam shelf (Steam's ARM64 client in Big Picture, through FEX and gamescope) stay; after Steam, ROCKNIX's script starts `essway`, which now runs the menu (`essway.service.d/banana.conf`) |
| Your Steam library (QR or password sign-in, the library, downloads) | added | `steam` as on BananaOS: DepotDownloader's linux-arm64 build (glibc, as ROCKNIX), fetched once and checked; python3, curl, unzip are in ROCKNIX's image |
| The Thor button (a press: the menu over a frozen game; held: the quick menu) | hooks | `hw-thor.conf`: `HW_HOME=yes`, `HW_HOME_KEY=316,102,194` (BTN_MODE through InputPlumber's pad, the AYN key as InputPlumber's KEY_HOME, or its own KEY_F24); both units delegate their control group; `HW_GAME_CGROUP` names ROCKNIX's `games.slice`, where `runemu.sh` puts each game's processes (in their own scope, out of the menu's unit). The menu's behaviour under sway is the menu's own work (`ui/src`, `rocknix/tests/*-sway.sh`) |
| The quick menu's live tiles | added | `sysstat` (CPU, GPU, temperatures, memory, battery, fan from `/proc` and `/sys`: the same kernel interfaces) |
| First-time setup: Wi-Fi, time zone, RetroAchievements, home server, Steam, the fan and lights, box art | added / before | the services mode for Wi-Fi and time zone; board `thor` gives the Controls and Steam slides (the home server one shows on any non-RG DS board) |
| Tuning (`docs/THOR-TUNING.md`) | ROCKNIX's / added | above (zram row) |
| Sound: PipeWire over AudioReach | ROCKNIX's | its PipeWire, WirePlumber, UCM and Bluetooth codecs |
| Wi-Fi, Bluetooth | ROCKNIX's | NetworkManager and BlueZ through the scripts' services mode |
| Web browser (WPE WebKit, Cog, wvkbd) | not on ROCKNIX | ROCKNIX's SM8550 image has no browser (only LibreELEC add-ons for other devices); WPE WebKit and Cog would have to become ROCKNIX packages (BananaOS's Buildroot recipes are the reference), the largest piece of work left |
| RomM, home server | added before | `banana-sync` |
| Files' servers (SMB, SFTP, FTP); sharing; SSH | partly | Files without SMB (`banana-fetch SMB=0`: libsmb2 isn't a ROCKNIX package; one would be needed); sharing is ROCKNIX's Samba, SSH its sshd |
| Store, Activity, Bshare, PortMaster | added before / ROCKNIX's | `banana-apps`, Activity, Bshare; PortMaster is ROCKNIX's |
| Updates: A/B slots from GitHub releases | changed | ROCKNIX's updater installs ROCKNIX's own releases, which would replace this image with EmulationStation. So `hw.conf`'s `HW_UPDATE=local` makes Settings > Update offer none; a new build goes in by its update tar in `/storage/.update` (ROCKNIX installs it at the next boot) |
| Fan, stick lights | ROCKNIX's | `handheld` over ROCKNIX's `fancontrol` and `ledcontrol` |
| Performance modes per system and game | added | `banana-perf` is now in the package (it wasn't: the modes never reached `run-rocknix`); its per-cluster caps, and the mode told to `runemu.sh` as ROCKNIX's per-game governor |
| The companions (GBA, DS) | added | their game data (`usr/share/banana/companions`) is now in the package; the DS companion still needs BananaDS's overlay in its SDL frontend |
| USB debugging (network, serial console, adb) | partly | `usb-debug` drives ROCKNIX's own gadget (`usbgadget`): its NCM network (the Thor on 169.254.170.2, the computer .1) with ROCKNIX's sshd and root password, at boot unless the port is in ROCKNIX's file transfer mode or `usb_debug = off`. ROCKNIX's SM8550 kernel has NCM, ECM, RNDIS, mass storage and FunctionFS but not `USB_CONFIGFS_ACM`, and the image has no adbd, so the serial console and adb are BananaOS's image only |
| `data-link`, `ra-standalone` | ROCKNIX's | they serve BananaOS's own launchers of the standalone emulators; on ROCKNIX its start scripts keep their folders on ext4 and its `cheevos_*.sh` log the standalone emulators in from its settings, which `run-rocknix` fills from the menu's account |

## Building it

On a Linux x86_64 PC with Docker, about 150 GB free and 16 GB of RAM or more. ROCKNIX refuses to build as root.

```sh
git clone https://github.com/JorreFog/BananaLinux
cd BananaLinux
git checkout claude/pensive-rubin-cy7ro4                              # or main once it's merged
git clone https://github.com/ROCKNIX/distribution ../rocknix
git -C ../rocknix checkout "$(sed -n 's/^commit=//p' rocknix/ROCKNIX)"   # 4fc79d7c
sh rocknix/tests/run.sh                                                  # the checks, a minute
sh tools/rocknix-banana.sh ../rocknix SM8550
cd ../rocknix
make docker-SM8550
```

The image is `release/ROCKNIX-SM8550.aarch64-*.img.gz` (with an update `.tar` beside it). Write it to a microSD
card (balenaEtcher, Raspberry Pi Imager or `dd`). The Thor needs OpenABL once (ROCKNIX's Thor page,
https://rocknix.org/devices/ayn/thor-sm8550; back up the stock ABL first). Card in, power on: ROCKNIX's first boot
makes its storage partition, then the menu comes up on both screens.

One package again after a fix here: `sh tools/rocknix-banana.sh ../rocknix SM8550`, then in ROCKNIX's build
container (`make docker-shell`): `PROJECT=ROCKNIX DEVICE=SM8550 ARCH=aarch64 ./scripts/clean banana-frontend` and
`... ./scripts/build banana-frontend`, then `make docker-SM8550` for the image. A new build onto a Thor that already
runs one: the update `.tar` from `release/` into `/storage/.update` (over SSH or Samba), then a restart.

### What was checked here, and what only the Thor can tell

Checked on a PC, without a Thor and without an image:

- the installer against ROCKNIX at `4fc79d7c`: it runs, it is idempotent, and every package and list lands where
  ROCKNIX's build looks; the move from `6cf94dba` changed nothing the package uses (in `projects/ROCKNIX` the two
  commits differ in the mainline kernel's version, an `input_sense` message, a dropped 7.2 clock patch and the new
  armGBA core; the SM8550 device folder is the same);
- Banana's RetroArch patches with ROCKNIX's patch rules: ROCKNIX's own patches, then `1001` and `1005`, all apply
  to ROCKNIX's RetroArch (`f726f095`), with the battery one after them;
- melonDS: `bananads-sa` unpacks `melonds-sa`'s source, the same commit BananaDS pins (`906e9ebb`);
- every package the frontend depends on exists in ROCKNIX's tree under that name (SDL2, freetype, libdrm, zlib,
  alsa-lib, Python3, curl, openssl, libssh2; SDL2_ttf for mame-gnw);
- `rocknix/tests/run.sh`: the converter (DOS renamed, the PC games hub), the services mode, the backlights, the
  performance modes, the handheld tool, the game page's options, the register script, and `thor.sh` (the board and
  profile per handheld, pc-games' folder mode, run-pc under sway, usb-debug over usbgadget, tune with the Thor's
  profile, the update guard, the installer on a made-up checkout); `system/tests/*-test.sh` for BananaOS's own image;
- a build of `banana-frontend` with ROCKNIX's toolchain was started in ROCKNIX's build container
  (`ghcr.io/rocknix/rocknix-build`, `scripts/build banana-frontend` for SM8550) on a 4-core machine with 16 GB free:
  it downloaded and built ROCKNIX's first host tools (openssl, make, pkg-config) and stopped in `cmake:host`, its
  compilers killed from outside the build. The rest of the chain before the package (the cross toolchain, glibc,
  Mesa, systemd, PulseAudio, SDL2, Python3) needs more disk and hours than that machine had, so the package itself
  has not been compiled with ROCKNIX's toolchain yet.

Only the Thor can tell:

- that `rocknix-device` sees the model as `AYN Thor` (`cat /run/banana/board` says `thor`);
- the Thor button's key code under InputPlumber (`evtest` on InputPlumber's pad and keyboard, then `HW_HOME_KEY`);
- that the second output and its touchscreen come on with the menu (ROCKNIX's sway config starts them off on a Thor);
- the backlights' names and order, the output names (`WLR_CON`) against `BANANA_TOP`, the touchscreens' names;
- Box64 + Wine + DXVK under ROCKNIX's sway and Mesa (a first game from "Set up PC games"); DepotDownloader's start on
  ROCKNIX's glibc and OpenSSL (`/usr/libexec/banana/steam status`, then a QR sign-in);
- USB debugging: the port in device mode with the menu's profile, `ssh root@169.254.170.2` from a computer;
- the package's build against ROCKNIX's toolchain end to end.

### Hand-off: a prompt for building it on your PC

Paste this to an AI assistant (or follow it yourself) on the PC that builds the image:

> Build the AYN Thor's ROCKNIX image with BananaOS in it, from the BananaLinux repository
> (https://github.com/JorreFog/BananaLinux, branch `claude/pensive-rubin-cy7ro4`). Read `docs/THOR-ROCKNIX.md` first,
> then follow its "Building it" section exactly: ROCKNIX at the commit in `rocknix/ROCKNIX`, `sh rocknix/tests/run.sh`,
> `sh tools/rocknix-banana.sh ../rocknix SM8550`, `make docker-SM8550` in the ROCKNIX checkout, as a normal user
> (not root). Rules:
>
> 1. When a package fails, fix its recipe in the BananaLinux repository (`rocknix/packages/...`,
>    `tools/rocknix-banana.sh`, `tools/rocknix-register.py`, the scripts in `system/rootfs/usr/libexec/banana`),
>    never in ROCKNIX's checkout or its `build.*` folder; run `tools/rocknix-banana.sh` again and rebuild that
>    package (`./scripts/clean <package>` and `./scripts/build <package>` in `make docker-shell`, with
>    `PROJECT=ROCKNIX DEVICE=SM8550 ARCH=aarch64`).
> 2. Don't drop a feature to get a green build: no package taken off the lists, no option turned off, no test
>    skipped. If something can't be made to work, stop and say what and why.
> 3. Run `sh rocknix/tests/run.sh` and `make test` before each commit; commit each fix on its own with what it fixes.
> 4. At the end, report: the image's file name and size, each fix you made (file, what was wrong, how you fixed it),
>    any warning worth a look, and anything left undone.

## The Thor's screen shapes

The menu was drawn for 4:3 (the RG DS's 640x480 and 1024x768). The Thor's top screen is 16:9 (1920x1080) and its
bottom one is nearly square (1240x1080). The menu now fills both, in three layers:

1. **Each screen has its own size.** `SoftBackend::setScreens`, from `Display::screenWidth/Height`: the panel's or
   window's upright size.
2. **The 4:3 canvas is staged in each screen.** The menu still lays out on its 640x480 grid (1440x1080 px on the
   Thor). The backend places that grid on each screen as large as it fits, centred: full size on the top, scaled to
   1240x930 on the bottom.
3. **Backgrounds fill the screen.** `Backend::stageArea` and `rnds::screenArea` give each screen's whole area. A
   screen's "screen" box (the default menu, Settings, the DS companion), its wallpaper (scaled to cover) and any solid
   fill over the whole canvas are drawn over all of it. There are no black bars.

On top of that, the views use the wide top screen where it counts. The default menu's status bar is anchored to the
screen's edges (Wi-Fi, battery and badge on the left, the clock on the right), and its big panel (the "sheet") spans
the screen's width. Everything placed relative to the middle stays centred.

`BANANA_STAGE=fit` goes back to the plain fit with black bars.

On the RG DS, a screen the canvas's shape, nothing changes. The home, library, game and Settings screens at 640x480
and 1024x768 come out pixel-identical to before. `make test` renders the Thor's shapes (`thor-*.png`) and checks each
screen is filled.

Still to make use of the extra room, view by view:
- **The bottom screen.** It is taller than 4:3 for its width: 557 grid units high instead of 480. The shelf, the
  lists and Settings' grid could take more rows.
- **The game list's top screen.** The cover and its details sit in the middle of the wider sheet. They could spread
  out.
- **Settings' top panel and the DS companion's pages.** They have the same fixed 604-wide boxes the sheet had.
- **Layout themes (`docs/MAKE-A-THEME.md`).** Their `W` and `H` are still the canvas's. Giving them the screen's
  would let a theme written with `W` reflow, but the bundled themes use fixed coordinates.

## Steps

### R0. An image that starts the menu

1. `tools/rocknix-banana.sh` on a ROCKNIX checkout, then `make docker-SM8550`.
2. Fix what the package build says: dependency names, the cross `pkg-config`, missing headers.
3. Flash and boot it the ROCKNIX way.
4. Check `bananasway.service` starts and essway stays down: `journalctl -u bananasway`,
   `/storage/.config/banana-data/banana/logs/banana-ui.log`.
5. Check the display lines in that log: screen 0 on the top output, screen 1 on the bottom.
6. If the output names don't match `WLR_CON`, set `BANANA_SDL_TOP` in `start_banana.sh`.

### R1. The menu works on the Thor

1. **Buttons.** The Thor's pad is ROCKNIX's InputPlumber device, or the RSInput evdev device. banana-ui opens every
   evdev device with buttons; check which one it reads, and that it doesn't double up.
2. **Touch on both screens.**
   - In a window, the touchscreen's coordinates are the output's. `BANANA_TOUCH` names the bottom touchscreen.
   - The top one is FocalTech on `i2c4`, the bottom one on `i2c_hub_3`.
   - The fit maps both.
3. **Sound.**
   - The menu plays to ALSA's `default`, which is PipeWire on ROCKNIX.
   - BananaOS's own volume and sleep scripts aren't on ROCKNIX, so the volume keys are ROCKNIX's (its input sense
     service).
4. **Settings.** Settings pages that run BananaOS's scripts (`/usr/libexec/banana/*`: Wi-Fi, Bluetooth, update,
   sharing, SSH) run them in their services mode on ROCKNIX (7.1 below): the same script names over ROCKNIX's
   NetworkManager, systemd units and settings, so the full menu works.

### R2. What only two screens give

1. While a game runs, the bottom window stays for the game panel and the companions (8.1 below): the menu closes
   its top window, the game's window takes that output (a sway rule keeps RetroArch there), and the panel's commands
   reach RetroArch through its network command port.
2. The DS emulators: ROCKNIX's melonDS places its two screens itself on a two-screen device; BananaDS opens a window
   on each (2.1 below).

### R3. Ship it

1. An upstream question for ROCKNIX: the frontend as a choice per device, not a fork (`docs/ROCKNIX-UPSTREAM.md`).
2. Until then, the Thor image is ROCKNIX's build with this package (CI's `rocknix-thor` job), and its updates are
   ROCKNIX's update mechanism with this package's version.

## What the earlier Thor work still gives

- **`panelfit`.** It fits the canvas in the SDL windows exactly as it does on KMS panels.
- **The hardware research and `hw-probe`.** Both are useful on ROCKNIX too.
- **The Buildroot board (`make thor`) and its imports from Banana Pocket.** It has since grown into BananaOS's own
  image for the Thor (`docs/THOR-OS.md`), and is the reference for the kernel and sound facts.

## To make it complete

What the Thor version needed, item by item, and where each stands. **Reuse** means ROCKNIX (or BananaOS) already had
it and Banana's menu drives it. **Built** means it was written here. Everything below is in this repository and
checked by `make test` (`rocknix/tests/run.sh`, `ui/tests/run.sh`) without a Thor. **On the Thor** marks what only
the handheld can confirm; nothing here has run on one yet, and no image has been built.

### 1. Every emulator ROCKNIX can run

| # | What | How | Status |
|---|---|---|---|
| 1.1 | Every ROCKNIX system on the shelves | `es-systems-to-banana.py` lists every system in ROCKNIX's `es_systems.cfg` and starts each through `runemu.sh` (`run-rocknix`) | done |
| 1.2 | The emulator and core per system and per game | The game page (X on a game): a **For** row (this game or its whole system) and an **Emulator** row with ROCKNIX's choices for the system. `rocknix-options` keeps them in ROCKNIX's `system.cfg` as EmulationStation does (`<system>.emulator/.core`, `<system>["<rom>"].emulator/.core`); `run-rocknix` starts the game with them | done |
| 1.3 | Per-game options | The rows the chosen emulator and core have in ROCKNIX's `es_features.cfg`: its own `<feature>`s and the shared ones (aspect ratio, pixel perfect, smoothing, shader, bezel, run-ahead, rewind, auto save, RetroAchievements), as the keys `setsettings.sh` and the standalone start scripts read | done |
| 1.4 | Standalone emulators on two screens | Reuse: ROCKNIX's start scripts already place melonDS's and Azahar's windows on two-screen devices (`DEVICE_HAS_DUAL_SCREEN`), and its sway rule moves any window titled `...Bottom...`/`Screen 2` to the second output. Banana's own emulators use the same rule (section 2) | done; on the Thor |
| 1.5 | PortMaster, Tools, scripts | Reuse: Ports run through `runemu.sh` (PortMaster as the emulator), Tools through their own command (`foot` under sway), both passed through by the converter | done; on the Thor |
| 1.6 | Scraping, box art, game names | ROCKNIX's scraper's art is read (the library reads `gamelist.xml`'s `image` and `thumbnail`); Banana's `banana-scrape` is in the package and knows ROCKNIX's systems' libretro names | done |

### 2. Banana's own emulators and apps

Under sway the panels are the compositor's, so each of Banana's programs draws in windows there: a full-screen window
for each screen, the bottom one's title ending in "Bottom" (ROCKNIX's own rule moves it to the second output). Each is
a ROCKNIX package under `rocknix/packages/`, listed by `tools/rocknix-register.py` (the device's
`ADDITIONAL_PACKAGES`, an `add_emu_core` line before each system's `add_es_system`, never the default, and its options
in `es_features.cfg`).

| # | What | How | Status |
|---|---|---|---|
| 2.1 | BananaDS | `bananads-sa`: its SDL frontend with a two-window mode, pads through SDL (InputPlumber's), the stylus a touch on the bottom window (patch 0001); melonDS from ROCKNIX's `melonds-sa` source, the same commit. The game page's 3D resolution. Its KMS frontend (in-game menu, the DS companion overlay) needs DRM master, which sway holds | done (SDL route); `rocknix/tests/bananads-sway.sh` runs it under sway |
| 2.2 | SuperDrastic with DraStic | Reuse ROCKNIX's `drastic-sa`, already DraStic under sway. SuperDrastic is DraStic drawn onto the RG DS's panels through KMS; there is nothing for it to add under a compositor | ROCKNIX's DraStic |
| 2.3 | BananaFront | `bananafront-sa`: an SDL display backend (`video_sdl.c`, `make sdl`; patch 0001), cores named `bananafront-<core>` so `runemu.sh` starts `start_bananafront.sh` | done; run under sway by hand |
| 2.4 | BananaGBA, BananaNES, BananaSNES | `*-lr` RetroArch cores beside ROCKNIX's (no RG DS profile-guided optimisation: other CPU, other GCC) | done |
| 2.5 | Banana's RetroArch patches | ROCKNIX's RetroArch is newer: 0005 (RGUI in Ukrainian) applies as it is, 0001's ALSA part is ported (ROCKNIX has the AUDIO_REINIT command already), 0006 is in its rcheevos already, 0002-0004 are BananaOS's own (the DRM context, the card's updater, the image's cores) | done |
| 2.6 | Game & Watch through MAME | `mame-gnw` with BananaOS's patches; a two-screen game is two windows, the second moved by a rule `start_mamegnw.sh` sets | done; on the Thor |
| 2.7 | The companions | The GBA ones work as on BananaOS: RetroArch's memory replies come back through `ra-netcmd`, BananaFront's shared-memory export through the environment. The DS companion draws through BananaDS's KMS overlay, which the SDL route doesn't have | GBA done; DS needs BananaDS's overlay in its SDL frontend |
| 2.8 | The Store, Store apps, Bank & Trade, Döda Kvarter, Activity, Bshare, Files | `banana-apps` (the Store and Döda Kvarter, with a two-window mode for their SDL layer), Activity's SDL route (SDL opened at run time), Bshare, Sync and Fetch built into the frontend; the launchers take the SDL route when the menu's display is SDL; `/data` is a folder on `/storage`. Bank & Trade runs in one window (its own layout) | done; on the Thor |

### 3. Performance modes

| # | What | How | Status |
|---|---|---|---|
| 3.1 | Mode per system and per game | `banana-perf`'s mode written as ROCKNIX's per-game `cpugovernor` and `gpuperf` (`run-rocknix`), which `runemu.sh` applies. `banana-perf` itself is in the package now (it was missing, so no mode reached `run-rocknix`) | done |
| 3.2 | Modes that fit the Thor | `banana-perf`'s multi-cluster branch: caps per cluster (A510, A715, X3) as shares of each one's top clock, and a GPU level | done; on the Thor |
| 3.3 | A quick toggle while playing | The game page's Performance row; on the bottom screen while playing, the game panel (8.1) | game page done |

### 4. Fan control

| # | What | How | Status |
|---|---|---|---|
| 4.1 | Fan profiles | Reuse: ROCKNIX's SM8550 `fancontrol` with its `cooling.profile` (quiet, moderate, aggressive, custom). (An earlier version of this page said ROCKNIX had no Thor fan control: it has.) `handheld fan` sets the profile and restarts the service | done |
| 4.2 | Settings page | Sleep & power > Fan, lights & speed, shown when the handheld has them (`handheld caps`) | done |

### 5. RGB lights round the sticks

| # | What | How | Status |
|---|---|---|---|
| 5.1 | Colour, brightness, off, the battery's colour | Reuse the Thor's `ledcontrol` and `analog_sticks_ledcontrol`, ROCKNIX's `led.color` kept the same | done |
| 5.2 | Effects | Breathing (a small loop over the rings' sysfs), each system's colour at a game's start | done |

### 6. Brightness for each screen

| # | What | How | Status |
|---|---|---|---|
| 6.1 | A level per screen | `backlight`: the top screen's backlight found by the DSI hosts' order, a level each, ROCKNIX's `display.brightness`/`brightness2` kept the same | done; names on the Thor |
| 6.2 | The brightness hotkeys | ROCKNIX's hotkeys step each screen from where it is (the same settings) | done |
| 6.3 | The bottom screen off in games that don't use it | Settings > Display > Bottom screen in games (auto: dark for one-screen systems), never under the game panel | done |

### 7. The system

| # | What | How | Status |
|---|---|---|---|
| 7.1 | Settings pages that call BananaOS's scripts | Their services mode (`HW_SERVICES=systemd`): Wi-Fi through NetworkManager, Bluetooth, SSH, Samba, updates through `rocknix-update`, the volume through PipeWire | done |
| 7.2 | Sleep and the lid | Reuse ROCKNIX's fake suspend on SM8550 (the power button and the lid: the game frozen, the screens and sound off, timed shutdown). Sleep & power keeps its one row that maps: Power off after sleeping, as ROCKNIX's timed shutdown (`handheld poweroff-after`) | done; on the Thor |
| 7.3 | Input | InputPlumber's virtual pad read, its sources ignored (`BANANA_INPUT_IGNORE`); the confirm button choice | done; on the Thor |
| 7.4 | Touch on both screens | Both touchscreens by name (the device tree's labels), mapped through each screen's fit | done; on the Thor |
| 7.5 | First boot | Banana's setup pages work as they are: Wi-Fi through NetworkManager, the time zone given to ROCKNIX too (`timezone`: each of Banana's zones as an IANA zone, `rocknix-config tz`), the RetroAchievements account given to ROCKNIX's RetroArch at a game's start | done |
| 7.6 | Image build and updates | CI's `rocknix-thor` job (manual: workflow_dispatch with `thor_image`): ROCKNIX at the pin, `tools/rocknix-banana.sh`, `make docker-SM8550`, the image as an artifact (ROCKNIX at `rocknix/ROCKNIX`'s commit). Updates: this image's own builds' update tars in `/storage/.update`; ROCKNIX's releases are not offered (`HW_UPDATE=local`: they would bring EmulationStation back) | defined; not run |
| 7.7 | Upstream | `docs/ROCKNIX-UPSTREAM.md`: the hooks to ask ROCKNIX for, so this needn't be a fork | drafted; not sent |

### 8. The bottom screen while playing (BananaOS's edge)

| # | What | How | Status |
|---|---|---|---|
| 8.1 | Banana's game panel on the bottom while a game plays on the top | The menu closes its top window for the game and keeps the bottom one (`leaseSdl`); RetroArch is kept on the top output; the panel's commands go to RetroArch's network command port (`ra-netcmd`), BananaFront reads them itself. `rocknix/tests/gamepanel-sway.sh` | done; on the Thor |
| 8.3 | The Thor button over a game | Tap: the game frozen (the menu's own `banana-game` group under its unit, plus ROCKNIX's `games.slice`, where `runemu.sh` puts each game; SIGSTOP as a fallback), the menu's windows reopened over it, fullscreen and focused through sway's IPC; a second tap closes them, gives the game back its fullscreen views and focus, and thaws it. Hold 3 s: the quick menu slides up on the bottom output while the game keeps running (over the game panel, the menu's own bottom window; over a two-screen game, a window raised over it, sliding over a `grim` picture of the game's bottom screen). `rocknix/tests/home-sway.sh` (headless sway at the Thor's sizes, a stand-in game whose frames are counted, the key injected) | done; on the Thor |
| 8.2 | The menu's views filling the Thor's shapes | Done so far: every screen filled, the status bar and the sheet spanning the top screen. Still to do, view by view: the bottom screen's extra rows (557 grid units high instead of 480: Settings' lists, the shelf), the game list's top screen, Settings' top panel. Each view lays its rows, footer and touch areas out on fixed coordinates, so this is a per-view reflow, done carefully to keep the RG DS pixel-identical | partly |

### What only the Thor can tell

Before an image is worth flashing, these are the open questions (`.claude/skills/thor-bringup`):

- the backlights' names and order, and the output names (`WLR_CON`) against `BANANA_TOP`;
- InputPlumber's device names for `BANANA_INPUT_IGNORE`, and both touchscreens' names;
- that SDL's Wayland windows land on the outputs as they do under headless sway, and the sway rules for fullscreen;
- the package builds against ROCKNIX's toolchain (dependency names, the cross `pkg-config`);
- the Thor button under InputPlumber: which of 316, 102, 194 it sends, that the menu's grab keeps the game from
  seeing presses, and whether the button also opens RetroArch's own menu;
- that freezing `games.slice` stalls nothing else ROCKNIX puts there, and that sway raises the menu over a frozen
  game (it waits about 200 ms for the frozen client) and matches the outputs by position on the real panels;
- whether a two-screen emulator drawing both windows from one thread stalls while the quick menu covers its bottom
  window;
- the Thor's own pieces listed under "Building it" above: the board, the Thor button's codes, the second output
  coming on, PC games and Steam, USB debugging.

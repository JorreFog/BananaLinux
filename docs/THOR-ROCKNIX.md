# BananaOS's menu on ROCKNIX: the AYN Thor

**Decided 2026-11-12:** the AYN Thor version is ROCKNIX's SM8550 image with BananaOS's dual-screen menu as its
frontend. It is not BananaOS's own system on the Thor. (`docs/THOR-PORT.md` describes the BananaOS-on-Buildroot board
that came first. It stays in the tree as reference and as a fallback; its hardware findings hold either way.)

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

Checked without a Thor:

- **`ui/tests/sway-test.sh`** runs sway headless with two outputs the Thor's size (1920x1080, 1240x1080). It starts
  the menu and screenshots both outputs. The top screen lands on the 1920x1080 output and the bottom on the 1240x1080
  one, each fitted with black bars.
- **`rocknix/tests/run.sh`** checks the converter against a sample in ROCKNIX's `es_systems.cfg` shape. A headless
  banana-ui read the converted list and launched a game from it.
- **The installer** ran on a ROCKNIX checkout at `6cf94dba` and is idempotent.

**Not yet:** a ROCKNIX image build with the package, and a run on a Thor.

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
- **The Buildroot board (`make thor`) and its imports from Banana Pocket.** They're a fallback if the ROCKNIX route
  hits a wall, and a reference for the kernel and sound facts.

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
| 3.1 | Mode per system and per game | `banana-perf`'s mode written as ROCKNIX's per-game `cpugovernor` and `gpuperf` (`run-rocknix`), which `runemu.sh` applies | done |
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
| 7.6 | Image build and updates | CI's `rocknix-thor` job (manual: workflow_dispatch with `thor_image`): ROCKNIX at the pin, `tools/rocknix-banana.sh`, `make docker-SM8550`, the image as an artifact. Updates are ROCKNIX's, with this package in them | defined; not run |
| 7.7 | Upstream | `docs/ROCKNIX-UPSTREAM.md`: the hooks to ask ROCKNIX for, so this needn't be a fork | drafted; not sent |

### 8. The bottom screen while playing (BananaOS's edge)

| # | What | How | Status |
|---|---|---|---|
| 8.1 | Banana's game panel on the bottom while a game plays on the top | The menu closes its top window for the game and keeps the bottom one (`leaseSdl`); RetroArch is kept on the top output; the panel's commands go to RetroArch's network command port (`ra-netcmd`), BananaFront reads them itself. `rocknix/tests/gamepanel-sway.sh` | done; on the Thor |
| 8.2 | The menu's views filling the Thor's shapes | Done so far: every screen filled, the status bar and the sheet spanning the top screen. Still to do, view by view: the bottom screen's extra rows (557 grid units high instead of 480: Settings' lists, the shelf), the game list's top screen, Settings' top panel. Each view lays its rows, footer and touch areas out on fixed coordinates, so this is a per-view reflow, done carefully to keep the RG DS pixel-identical | partly |

### What only the Thor can tell

Before an image is worth flashing, these are the open questions (`.claude/skills/thor-bringup`):

- the backlights' names and order, and the output names (`WLR_CON`) against `BANANA_TOP`;
- InputPlumber's device names for `BANANA_INPUT_IGNORE`, and both touchscreens' names;
- that SDL's Wayland windows land on the outputs as they do under headless sway, and the sway rules for fullscreen;
- the package builds against ROCKNIX's toolchain (dependency names, the cross `pkg-config`).

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
| Data | `/storage` | the menu's in `/storage/.config/banana`, games in `/storage/roms` |

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
   `/storage/.config/banana/logs/banana-ui.log`.
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
   sharing, SSH) find nothing. Two ways forward:
   - Map them to ROCKNIX's tools (`connmanctl`, `rocknix-settings`, `systemctl`) behind the same script names, in the
     package.
   - Hide them and point at ROCKNIX's own settings, which live in EmulationStation.

   The second is quicker; the first gives the full menu.

### R2. What only two screens give

1. While a game runs, keep the bottom window for the game panel and the companions, BananaOS's Pokémon pages and
   the like, instead of closing both.
   - The game goes fullscreen on the top output by sway rule:
     `for_window [app_id="..."] move to output $WLR_CON, fullscreen`.
   - This replaces lowerdeck's save/load/resume buttons. Banana's game panel already sends RetroArch those commands
     on the RG DS.
2. The DS emulators: ROCKNIX's melonDS has a dual-screen fullscreen patch (`002-enable-dual-screen-fullscreen`).
   Put its two screens on the two outputs.

### R3. Ship it

1. An upstream question for ROCKNIX: the frontend as a choice per device, not a fork.
2. Until then, the Thor image is ROCKNIX's build with this package, and its updates are ROCKNIX's update mechanism
   with this package's version.

## What the earlier Thor work still gives

- **`panelfit`.** It fits the canvas in the SDL windows exactly as it does on KMS panels.
- **The hardware research and `hw-probe`.** Both are useful on ROCKNIX too.
- **The Buildroot board (`make thor`) and its imports from Banana Pocket.** They're a fallback if the ROCKNIX route
  hits a wall, and a reference for the kernel and sound facts.

## To make it complete

What the Thor version still needs, item by item. **Reuse** means ROCKNIX (or BananaOS) already has it and Banana's
menu only has to drive it. **Build** means it doesn't exist yet. Sizes are rough: **S** a day or so, **M** a few days,
**L** a week or more.

### 1. Every emulator ROCKNIX can run

| # | What | How | Size |
|---|---|---|---|
| 1.1 | Every ROCKNIX system on the shelves | Reuse: `es-systems-to-banana.py` already lists every system in ROCKNIX's `es_systems.cfg` (all of its about 170 emulator packages) and starts each through `runemu.sh` | done |
| 1.2 | Choosing the emulator and core per system and per game | Reuse ROCKNIX's settings (`set_setting <system>.emulator / .core`, per game `<system>["<rom>"].core`, which `runemu.sh` reads). Build: Banana's game options page lists the `<emulators>` the converter already reads and writes those keys | M |
| 1.3 | Per-game options ROCKNIX offers (shaders, aspect, rewind, run-ahead, overlays) | Reuse `setsettings.sh`'s keys. Build: a Banana options page over them, taking its list from ROCKNIX's `es_features.cfg` | M |
| 1.4 | Standalone emulators on two screens (melonDS's dual-screen patch, Azahar for 3DS, the DS and 3DS ones' layouts) | Reuse their dual-screen modes. Build: sway rules that put each emulator's windows on the right output | M |
| 1.5 | PortMaster, Tools, scripts | Reuse: they come through `es_systems.cfg` too (`run %ROM%`). Check each one's runner works when started from Banana | S |
| 1.6 | Scraping, box art, game names | Reuse ROCKNIX's `rocknix-scraper` media folders, or Banana's own `banana-scrape` | S |

### 2. Banana's own emulators and apps

These are BananaOS's, not ROCKNIX's. Each needs a ROCKNIX package, an entry in ROCKNIX's emulator list
(`add_emu_core`, so it shows up in `es_systems.cfg` and the converter takes it), and a way to draw under sway. Today
they draw on the panels themselves through KMS leases and Banana's SDL panels patch, and under sway they must become
Wayland clients.

| # | What | How | Size |
|---|---|---|---|
| 2.1 | BananaDS (DS) | Package it, register it as the `nds` system's emulator, draw its two screens on the two outputs (Wayland), keep its companion link | L |
| 2.2 | SuperDrastic with DraStic | The same. DraStic's licence allows it on ROCKNIX, which packages it already (`drastic-sa`); SuperDrastic's panels need the Wayland route | L |
| 2.3 | BananaFront | Package it, register it, Wayland | M |
| 2.4 | Banana's libretro cores (BananaGBA, BananaNES, BananaSNES) | Package them as ROCKNIX libretro cores (`add_emu_core <system> retroarch banana…`) | S |
| 2.5 | Banana's RetroArch patches (audio re-init, the cheevos fix, RGUI Ukrainian…) | Add them to ROCKNIX's RetroArch package | M |
| 2.6 | Game & Watch through MAME (`mame-gnw` with Banana's patches) | Package it, register it as `gameandwatch`, its two screens on two outputs | M |
| 2.7 | The Pokémon companions (GBA and DS) on the bottom screen | Build (part of 2.1 and 5.1): needs BananaDS and Banana's GBA core, which share game memory with the menu | M |
| 2.8 | The Store, Store apps, Bank & Trade, Dodakvarter, Activity, Bshare, Files | Package them. They are Banana's SDL or menu-own apps, and they need the Wayland route and ROCKNIX paths | M |

### 3. Performance modes

| # | What | How | Size |
|---|---|---|---|
| 3.1 | Mode per system and per game | Reuse: `runemu.sh` already sets ROCKNIX's `cpugovernor` and `gpuperf` per system and per game (`099-freqfunctions`). Build: Banana's modes (powersave, balanced, steady, performance) written as those settings | S |
| 3.2 | Modes that fit the Thor | Build: the Thor has three CPU clusters (A510, A715/A710, X3) and an Adreno devfreq. Define each mode as per-cluster clock caps plus a GPU level, the way `banana-perf` does for the RK3568 | M |
| 3.3 | A quick toggle in the game menu | Build: a page on the bottom screen while playing | S |

### 4. Fan control

| # | What | How | Size |
|---|---|---|---|
| 4.1 | Fan profiles (Auto, Quiet, Balanced, Max, Off when cool) | Build: the Thor's fan is `pwm-fan` on the PMIC with the device tree's thermal trips. ROCKNIX has no fan-control quirk for the Thor, only for other devices (`quirks/devices/*/bin/fancontrol`, as a model). A small service drives `hwmon` `pwm1` / `pwm1_enable` from the SoC temperature against a curve per profile | M |
| 4.2 | Settings page, and a toggle in the game menu | Build | S |

### 5. RGB lights round the sticks

| # | What | How | Size |
|---|---|---|---|
| 5.1 | Colour, brightness, off, the battery as a colour | Reuse the Thor's `ledcontrol`, `analog_sticks_ledcontrol` and `battery_led_status` (the HTR3212 multicolour LEDs, `rgb:l1-4`, `rgb:r1-4`), and ROCKNIX's settings keys `led.color` and `analogsticks.led`. Build: a Banana settings page with a colour picker and the battery mode | S |
| 5.2 | Effects (breathing, per-system colour, low-battery blink) | Build: a small service over the same sysfs | M |

### 6. Brightness for each screen

| # | What | How | Size |
|---|---|---|---|
| 6.1 | A level per screen | Build: ROCKNIX's `brightness` script sets every backlight to one value. Banana's bananad already keeps one level per screen (`BANANA_BACKLIGHT_TOP/BOTTOM`, `/data/banana/backlight`) and Settings already has the two sliders. On ROCKNIX: a small helper (or bananad's backlight part) over the Thor's two AMOLED backlights (names to read on the device, `hw-probe`) | S |
| 6.2 | The brightness hotkeys | Build: ROCKNIX's hotkey service changes all backlights together. Point it at Banana's helper, or keep its keys off and let Banana's handle them (the screen being looked at, or both) | S |
| 6.3 | The bottom screen off or dimmed in games that don't use it | Build | S |

### 7. The system

| # | What | How | Size |
|---|---|---|---|
| 7.1 | Settings pages that call BananaOS's scripts | Reuse ROCKNIX's: `wifictl` (Wi-Fi), `rocknix-bluetooth`, `rocknix-update`, `rocknix-settings` (SSH, Samba, services), `volume`, `timeinfo`. Build: the same script names in the package, mapped onto them, so the menu's pages work unchanged | M |
| 7.2 | Sleep and the lid | Reuse ROCKNIX's suspend (fake suspend on SM8550 for now, `suspendmode`). Build: the menu and the game paused, the screens off, Banana's quick resume | M |
| 7.3 | Input | Check one thing: ROCKNIX runs InputPlumber on the Thor, so banana-ui may see the pad twice (evdev and InputPlumber's virtual pad). Build: read only one of them | S |
| 7.4 | Touch on both screens | Check the top touchscreen's mapping under sway (ROCKNIX's touch calibration, `61-thor-ft5x06.hwdb`) | S |
| 7.5 | First boot | Build: Banana's setup pages wired to ROCKNIX's settings (language, Wi-Fi, time zone) | S |
| 7.6 | Image build and updates | Build: a CI job that builds ROCKNIX's SM8550 image with this package. Updates follow ROCKNIX's update channel with the package's version | M |
| 7.7 | Upstream | Ask ROCKNIX for a frontend choice per device, so this needn't be a fork | S |

### 8. The bottom screen while playing (BananaOS's edge)

| # | What | How | Size |
|---|---|---|---|
| 8.1 | The game on the top output, Banana's game panel on the bottom (save, load, the quick menu, the companions) instead of lowerdeck | Build: keep the bottom window open during a game, a sway rule for the game's window. Save and load reuse RetroArch's network commands, as lowerdeck does | M |
| 8.2 | The menu's own screens filling the Thor's shapes view by view (the bottom screen's extra rows, the game list's top, Settings' top panel) | Build: see "The Thor's screen shapes" above | M |

### Suggested order

1. **R0 and R1** (an image that starts the menu, which works) with 7.1, 7.3 and 6.1. Without these it isn't usable.
2. **3.1, 5.1 and 4.1.** Performance, RGB and fan, mostly reuse.
3. **1.2, 1.3, 8.1 and 2.4.** The emulators' choices, the bottom screen in games, Banana's cores.
4. **2.1, 2.2, 2.6 and 2.7.** Banana's DS emulators, Game & Watch and the companions under sway. This is the largest block.
5. **The rest**, then 7.6 for releases.

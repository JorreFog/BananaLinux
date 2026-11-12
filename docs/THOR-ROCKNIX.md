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

<p align="center">
  <img src="docs/img/boot-animation.gif" width="320" alt="Banana's boot animation on both panels: a pixel-art bunch of bananas drops onto the top screen and BANANA types itself in under it, while on the bottom screen a cartridge with the bunch on its label slides into the console, which clicks and lights up green">
</p>

<p align="center"><b>A light Linux for the Anbernic RG DS and RG DS Plus: boots straight into ROCKNIXDS Pixel's menu,
sleeps when you close the lid, and gives the games every cycle.</b></p>

> [!WARNING]
> Banana is at its start (0.1.0-dev). Everything here builds and is tested off the device (see *Status*), but no
> image has booted on an RG DS yet. Don't flash it expecting a working handheld; if you do, [docs/FIRST-BOOT.md](docs/FIRST-BOOT.md)
> says what to try and which logs to send back.

## What it is

Banana is a whole system for the RG DS, built with Buildroot: a mainline kernel with ROCKNIX's RG DS support,
busybox init, and one program on the panels. There is no systemd, no compositor, no EmulationStation and no
desktop: the root filesystem is a read-only squashfs of a few tens of megabytes, and everything you add (games,
saves, settings) goes on an exFAT partition any computer can open.

| | |
|---|---|
| **The menu** | ROCKNIXDS Pixel, the same engine (imported unchanged from ROCKNIXDS) drawn in software straight onto both panels by `banana-ui`: the system shelf, DS cartridges with their label art, box covers, play stats, the cartridge sliding into the console. Dark and light. Banana's mark is the BananaUI bunch redrawn as pixel art in the theme's icon style. |
| **Settings** | Start in the menu: theme, brightness, motion, sounds, clock, the boot animation, sleep when idle, the lid, the LEDs, the DS renderer, shader and 3D resolution, Wi-Fi (the networks around, the password on an on-screen keyboard), file sharing, the time zone, About, restart and shut down. In the same look, applied at once. |
| **Boot animation** | From banana-ui's first frame, while the library loads on another thread: the bunch drops onto the top panel and BANANA types itself in; on the bottom panel the BANANA cartridge goes into the console with the menu's own insert motion, clicks, and the power light turns green. Then it dissolves into the menu. |
| **Light** | The menu draws a frame in well under a millisecond when only one screen changes, skips any screen whose content didn't change, and sleeps in `poll()` between frames. While a game runs it closes the display, the input devices and the sound device: the game has the handheld to itself. |
| **Sleep** | Close the lid and it is asleep in a moment: the game paused where it was, the card flushed, the handheld suspended to RAM. Open it and the game carries on as the panels light up: the game and the menu are resumed first, Wi-Fi and the rest catch up behind them, and the devices resume in parallel. Woken with the lid shut (the power button in a bag) it goes straight back to sleep. While asleep it checks the battery every 4 hours and shuts down cleanly (the game saves first) before it could run flat. Ten idle minutes in the menu also sleep; hold power to shut down. The menu warns at 20%, 10% and 5%. (`bananad`, tested in `system/bananad/tests/sleep-test.sh`) |
| **File sharing** | Switch it on in the settings and the card is on your network as `\\<address>\banana` (Windows) or `smb://<address>/banana` (Mac): copy games in and they show up in the menu. The kernel's own SMB3 server, loaded only while it is on. |
| **Other systems** | RetroArch 1.22 on one panel (top by default; the other panel and its backlight go off), KMS + GLES, with its RGUI pixel menu and cores built for the RK3566's Cortex-A55 with their ARM64 recompilers: GBA (gpSP), Game Boy (Gambatte), NES (FCEUmm), SNES (Snes9x), Mega Drive (PicoDrive), PlayStation (PCSX ReARMed), N64 (Mupen64Plus-Next). |
| **Performance** | DS games run through [SuperDrastic](https://github.com/JorreFog/SuperDrastic) (and, when it is ready, BananaDS: its package, launcher and settings row are in place, see [docs/EMULATORS.md](docs/EMULATORS.md)): each DS screen straight to its own panel, 2x resolution, its own per-game CPU governor. Every system gets a clock profile (`banana-perf`); the menu runs at the lowest clocks. |
| **Boot** | U-Boot with no delay, a kernel with the panels, buttons, touch and sound built in (no module loading before the menu), no initramfs, root mounted by the kernel, and an init script that only mounts the data partition before the menu starts. |

<p align="center">
  <img src="docs/img/menu-home.png" width="248" alt="Home: the Nintendo DS selected on the system shelf, its games, played count and time on the top screen">
  <img src="docs/img/menu-library.png" width="248" alt="The DS library: game cartridges on the bottom screen, the selected game's stats on the top">
  <img src="docs/img/menu-ready.png" width="248" alt="The ready screen: the cartridge going into the console">
</p>
<p align="center">
  <img src="docs/img/menu-settings.png" width="248" alt="The settings screen: the selected setting's icon, name, value and description on the top screen; the list of settings as slots with left and right arrows on the bottom">
  <img src="docs/img/menu-settings-system.png" width="248" alt="The settings screen on the System section: the time zone set to London, Dublin, Lisbon, then About, Restart, Shut down">
  <img src="docs/img/menu-wifi.png" width="248" alt="Joining a Wi-Fi network: the password typed so far on the top screen, an on-screen keyboard on the bottom">
  <img src="docs/img/menu-battery.png" width="248" alt="The low-battery notice over the top screen: the battery icon nearly empty in red, Battery nearly empty: 4%, save your game now">
</p>
<p align="center"><sub>banana-ui's own frames (headless renders, <code>make test</code>), with a test library of made-up games.
Start opens the settings.</sub></p>

## Made for the RG DS and RG DS Plus

Banana runs on these two handhelds only, and is built around them:

- **Two panels, one menu.** Both 640x480 (or 1024x768) panels are driven directly; the menu, the boot animation and
  DS games each use them as the DS did. SuperDrastic sends each DS screen straight to its own panel.
- **60 Hz panels.** The RG DS's panels come up at 60.10 Hz; Banana's device tree carries ROCKNIXDS's measured
  retune to 60.0013 Hz, so DraStic's frames are paced without repeats. On the RG DS Plus (60.17 Hz as shipped),
  `banana-panel-tune` reads the real pixel clock on the first boot and retunes both panels to 59.999 Hz.
- **The clamshell.** Closing the lid sleeps (the hall sensor), opening it wakes the game where it was.
- **The LEDs**, as on ROCKNIX: green, red at 20%, blinking red at 5%, amber while charging, driven by the kernel's
  own LED triggers, with no polling script.
- **Sound with no sound server.** The RG DS's rk817 codec is set up as ROCKNIX sets it. On the RG DS Plus, playback
  goes to the AW88166 speaker amplifiers or to the headphones the moment they are plugged in, and the microphone
  (the DS's mic, through SuperDrastic) always records from the rk817. The volume keys follow the output.
- **DraStic tuned for it:** ROCKNIX's RG DS button map, 2x resolution, threaded 3D, SuperDrastic's Gengis Engine
  renderer and its per-game CPU governor on by default.
- **Remembered:** brightness (F + volume) and volume are restored at boot.

## Status

Built and checked in this repository's CI and by hand:

- `banana-ui` builds (x86-64 and aarch64) and renders all three views at both panel sizes; ROCKNIXDS Pixel's
  frames are pixel-identical to the engine's own output apart from Banana's logo. `make test` checks the
  navigation and the launch.
- `bananad` and `banana-panel-tune` build without warnings for aarch64; the tuner, run on the real device trees,
  leaves the RG DS's measured timing alone and brings the RG DS Plus's from 60.17 to 59.9994 Hz.
- The boot animation renders at both panel sizes (`make test` shoots it at three moments).
- The settings' Wi-Fi pages run against a stand-in `wifi` script in `make test`: the network list, a password typed
  by touch on the on-screen keyboard, joining, forgetting, open and WPA3-only networks, checked in the `wifi.conf`
  they write. The low-battery notice, the time zone (all 30 rules against the tz database) and a DS ROM's icon read
  out of a `.zip` are checked there too.
- `bananad`'s sleep runs against a made-up `/sys` in its own namespaces (`system/bananad/tests/sleep-test.sh`, in
  CI); the SSH script against a real dropbear.
- Both Buildroot defconfigs load in Buildroot 2026.02.3 with every option kept.
- RetroArch (with exactly the package's configure options) and all seven cores build and link for aarch64 against
  an arm64 sysroot here; banana-ui links against the real arm64 libraries, and the aarch64 banana-ui, run under
  QEMU, draws the boot animation, the menu and the settings the same as the x86 build (at most 1 colour level
  apart, from fused multiply-adds).
- ROCKNIX's 43 RK3566 kernel patches plus the input-polldev patch apply to Linux 7.2.7 as ROCKNIX applies them (patch -p1; the two Bluetooth patches are left out, Bluetooth being off in Banana's kernel), the kernel config
  fragment applies as written, and the whole kernel builds (a 25 MB `Image`) with the panels, joypad, touch, codec,
  amplifiers and battery built in; the RG DS and RG DS Plus device trees compile.

- The whole system boots end to end in QEMU (`tools/qemu-boot.sh`): the kernel, init, the first boot's
  partitioning and exFAT, bananad, the menu on two virtual panels, a game started from the library and the menu
  back, a shutdown from the settings, the card's logs afterwards. The CI's `boot` job runs it on every image it
  builds.

Not done yet: a full image build (it needs the network access Buildroot downloads with; the CI's `image` job does
it), and anything on the hardware: the boot time, suspend and resume of the panels and Wi-Fi, the panels' handover
to SuperDrastic, the RG DS Plus's sound routing and panel tuning on a real unit. See [docs/ROADMAP.md](docs/ROADMAP.md).

## Build

On Linux (Debian/Ubuntu: `build-essential bc bison flex libssl-dev libelf-dev unzip rsync cpio python3-dev swig
libgnutls28-dev`):

```sh
make rgds          # output/rgds/images/banana-rgds.img.gz
make rgds-plus     # output/rgds-plus/images/banana-rgds-plus.img.gz
```

The first build fetches Buildroot and everything it builds, and takes an hour or more. Write the `.img.gz` to a
microSD card with balenaEtcher. On the first boot Banana makes the data partition from the rest of the card (a few
seconds); then put games in `roms/nds` and so on from any computer (see `banana/README.txt` on the card).

The whole image boots in QEMU, with no handheld (`tools/qemu-boot.sh`, and the CI's `boot` job after every image
build): the card on an emulated SD host, two 640x480 panels on a virtio GPU, a keyboard for the buttons. It runs the
first boot (the data partition made from the rest of the card), screenshots both panels, starts a game from the
library, shuts down from the settings, and reads the card's logs:

```sh
tools/qemu-boot.sh output/rgds/images/banana-rgds.img.gz        # the test; screenshots and logs in qemu-out/
tools/qemu-boot.sh output/rgds/images/banana-rgds.img.gz --interactive   # a window: arrows, Enter = A, Backspace = B
```

Working on the menu needs no handheld:

```sh
make ui            # ui/banana-ui for this computer
make test          # renders the menu from a test library into ui/tests/out
ui/banana-ui --headless out/ --assets ui/assets/rnds --roms <roms> --data <dir> \
    --script "home 0; wait 2400; shot home; press a; wait 2400; shot lib"
```

## Layout

| Path | What |
|---|---|
| `ui/` | `banana-ui`: the KMS host (`src/`), ROCKNIXDS Pixel's engine (`rnds/`, imported by `tools/import-rnds.sh`), its assets, the headless test |
| `system/bananad/` | `bananad` (sleep, lid, power button, LEDs, sound routing, volume and brightness, battery guard) and `banana-panel-tune` |
| `system/rootfs/` | init, first boot, `banana-perf`, sleep hooks, the game launchers, default settings |
| `buildroot/` | the Buildroot tree: defconfigs, Banana's packages, the RG DS board (kernel patches, device trees, drivers, U-Boot, image layout) |
| `tools/` | `import-rnds.sh` (the menu engine from ROCKNIXDS), `sync-rocknix-hw.sh` (the hardware support from ROCKNIX), the pixel bunch, the logo and the boot animation's still layers |
| `docs/` | [ARCHITECTURE.md](docs/ARCHITECTURE.md): boot, partitions, sleep, performance and the menu in detail; [FIRST-BOOT.md](docs/FIRST-BOOT.md): the first boot on the hardware, and the logs it leaves |

## Credits

The menu is ROCKNIXDS Pixel, from [ROCKNIXDS](https://github.com/JorreFog/ROCKNIXDS). Banana's mark is the
banana bunch of [BananaUI](https://github.com/JorreFog/helios), redrawn as pixel art. The RG DS's kernel support,
device trees and drivers come from [ROCKNIX](https://rocknix.org), and DS emulation is DraStic by Exophase through
SuperDrastic. Details and licenses: [THIRD_PARTY.md](THIRD_PARTY.md). Banana contains no games, ROMs or BIOS files.

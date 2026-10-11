# How Banana works

## The card

| Where | What | Size |
|---|---|---|
| sector 64 (32 KiB) | U-Boot for the RK3568 (`u-boot-rockchip.bin`: Rockchip's DRAM init, U-Boot SPL, TF-A BL31, U-Boot) | ~1.5 MB |
| partition 1, `BANANA-BOOT` (FAT) | `Image`, the handheld's device tree, `extlinux/extlinux.conf` | 64 MB |
| partition 2, `banana-root` (squashfs, LZ4) | the system, read-only | tens of MB |
| partition 3, `BANANA` (exFAT) | made on the first boot from the rest of the card: `roms/`, `bios/`, `saves/`, `states/`, `banana/` (settings, logs, play stats, favourites), `drastic/`, `superdrastic/` | the rest |

The image only holds the first two partitions, so it is small and quick to write. `/usr/libexec/banana/firstboot`
moves the GPT's backup header to the end of the card, adds partition 3 and formats it (exFAT, so Windows and macOS
can read and fill it). The root is never written to; nothing on it can be corrupted by pulling the battery.

## Boot

1. The boot ROM loads U-Boot from sector 64. U-Boot is built with no boot delay, no environment and only the SD card
   as a target (ROCKNIX's Quartz64-based configuration for these handhelds, `board/rgds/uboot-*`).
2. U-Boot reads `extlinux.conf` and starts the kernel with `root=PARTLABEL=banana-root rootfstype=squashfs ro
   rootwait quiet`. There is no initramfs: the kernel mounts the root itself.
3. Everything the menu needs is built into the kernel: the two DSI panels (the generic DSI panel driver), the
   buttons (ROCKNIX's ADC joypad driver), the touchscreens, the RK817 codec and the speaker amplifiers, the battery.
   Wi-Fi and Panfrost are modules, loaded after the menu is up. No fbcon, no VTs, no boot logo.
4. busybox init runs `/etc/init.d/rcS`: mount `/proc`, `/sys`, `/run`, `/tmp`, the data partition; start
   `bananad`; set the menu's clocks. Wi-Fi, the clock and the rest go to the background.
5. init starts `banana-ui`, which shows the splash on both panels while it reads the library, then the menu.

What is worth measuring on the device: U-Boot to kernel, kernel to init (the MMC probe and `rootwait`), and init
to the menu's first frame. The kernel's `initcall_debug` and banana-ui's log (it logs the library scan's time)
give the last two.

## The menu: banana-ui

ROCKNIXDS Pixel is drawn by an engine that knows nothing about EmulationStation (`ui/rnds`, imported unchanged
from ROCKNIXDS by `tools/import-rnds.sh`): a *Source* gives it systems, games and status, a *Backend* draws its
textures, and it rasterizes everything else itself (CSS-like boxes, Pixelify Sans text, fitted pictures). In
ROCKNIXDS, ES is the Source and its GL renderer the Backend. In Banana:

- **Source** (`src/library.cpp`): the systems in `/etc/banana/systems.conf` (or the copy in `/data/banana/`),
  their icons and colours from the theme's `systems.cfg`, the games found in each system's folder, names and art
  from an EmulationStation `gamelist.xml` when one is there (a card scraped under ROCKNIX keeps its art) or from
  `media/box|cart|label/<rom name>.png`, and Banana's own play counts, play time and favourites.
- **Backend** (`src/soft.cpp`): software, on the CPU. Each screen's draw calls are recorded and compared with the
  last frame's; a screen whose list didn't change keeps its pixels (most animations move one screen). A screen
  that changed is drawn from black, mostly with copies: each texture's rows are stored as runs of transparent,
  opaque and blended pixels, so opaque runs are `memcpy`, transparent runs are skipped and only edges are
  blended. In the benchmark (`bench` in a headless script) an idle frame costs well under a millisecond on a PC.
- **Display** (`src/kms.cpp`): KMS dumb buffers, two per panel, page flips on vblank. The screens are composed in
  cached memory and only changed rows are copied into the (uncached) scanout buffer.
- **Main loop** (`src/main.cpp`): `poll()` on the buttons, the touchscreen, the DRM device (flip events), the
  picture worker and signals, with a timeout from the engine's next animation step or the next minute (the
  clock). Nothing runs while nothing changes.

Starting a game: banana-ui writes the game's pid to `/run/banana/game.pid`, closes the display, the input
devices and the sound device, sets the system's clock profile, and runs its `run` command. When the game exits it
takes the panels back, records the play, and shows the game's library again.

## Sleep: bananad

`bananad` reads every input device next to the menu and the games (evdev allows several readers; it grabs
nothing):

- **Power button** (short press), **lid closed** (the hall sensor, `SW_LID`), or **10 minutes idle in the menu**:
  suspend. The game's process group gets `SIGSTOP`, `/etc/banana/sleep.d/*` run with `pre` (Wi-Fi down and its
  driver unloaded: the RTL8821CS doesn't survive a suspend otherwise), `deep` is chosen in
  `/sys/power/mem_sleep` and `mem` written to `/sys/power/state`. The device tree marks the lid and the power key
  as wakeup sources. After waking, the hooks run with `post`, the game gets `SIGCONT`, banana-ui gets `SIGUSR1`
  (it shows its panels again), and the button press that woke the handheld is ignored.
- **Power held 2 s**: the game gets `SIGTERM` (to write its saves), then a clean shutdown.
- **Volume keys**: the mixer; **F + volume**: both backlights.
- **Battery** at 3% and discharging for two readings in a row: clean shutdown.

All of it is set in `/etc/banana/bananad.conf`, overridden by `/data/banana/bananad.conf`.

## Clocks: banana-perf

| Profile | CPU | GPU | For |
|---|---|---|---|
| `ui` | schedutil, at most ~1.1 GHz | powersave | the menu |
| `powersave` | schedutil, at most ~1.4 GHz | on demand | 8-bit and 16-bit systems |
| `balanced` | schedutil, full range | on demand | GBA, SNES, PlayStation |
| `performance` | performance | performance | N64, ports |
| `superdrastic` | performance (SuperDrastic's own governor takes over: the lowest clock with no dropped frames, per game) | on demand | Nintendo DS |

## Hardware support

`buildroot/board/rgds` is ROCKNIX's RK3566 support at a pinned commit (`ROCKNIX`), copied by
`tools/sync-rocknix-hw.sh`: its kernel patches in its own order, the RG DS and RG DS Plus device trees, its kernel
config (Banana's changes are in `linux-banana.fragment`), the generic DSI panel and joypad drivers (built in via
`external.mk`), the input-polldev patch the joypad driver needs, its U-Boot patches, and the RK3568 BL31 it boots
with. A newer ROCKNIX is one command and a diff.

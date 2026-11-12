# BananaOS for the AYN Thor (the Buildroot image)

BananaOS's own image for the AYN Thor (Snapdragon 8 Gen 2: the Base, Pro and Max). It is built with Buildroot like
the RG DS's, from `buildroot/configs/banana_thor_defconfig`, and boots to BananaOS's menu on both screens with nothing
else running that the menu doesn't need.

There are two ways to run BananaOS on a Thor in this repository:

| | This image | The ROCKNIX route (`docs/THOR-ROCKNIX.md`) |
|---|---|---|
| What it is | BananaOS itself, built for the Thor | ROCKNIX's image with BananaOS's menu as its frontend |
| Idle memory | lowest: no compositor, no systemd, no EmulationStation | ROCKNIX's services plus sway under the menu |
| Emulators | BananaOS's packages (`docs/THOR-EMULATORS.md`) | everything ROCKNIX ships |
| State | written, checked on a PC, **never booted** | written, checked on a PC, **never booted** |

**Nothing here has run on a Thor.** What was checked without one is listed at the end.

## How it is put together

- **Boot.** OpenABL (flashed once to the Thor's `abl_a`/`abl_b`, the stock ABL backed up first) starts the kernel from
  the card's boot partition. The kernel is mainline 7.2 with ROCKNIX's SM8550 patches. The system is a read-only
  squashfs in one of two slots (3 GB each), updated A/B; the rest of the card is the exFAT data partition, readable
  on any computer.
- **Init.** busybox init and one script (`rcS`): udev, D-Bus's system bus, the sound server, `bananad` (power key, lid,
  volume, sleep, backlights), then the menu. No systemd, no login, no display manager.
- **The menu.** `banana-ui` draws on both panels through KMS itself: the top 1920x1080, the bottom 1240x1080, each
  turned as the kernel says. The Thor's default theme is **Thor OLED** (true black for the AMOLED panels).
- **Games.** RetroArch runs on KMS too, on one panel through a DRM lease while the menu keeps the other for its game
  panel (save, load, the companions).
- **Programs that need a window.** A standalone emulator with a Qt window, the web browser and Wine draw through
  Wayland. `run-wayland` starts a compositor (sway without its bar and tray, `package/sway-lite`) around that one
  program, on both panels, and it is gone when the program ends. A window titled like a second screen ("Bottom",
  "Screen 2", "GamePad"...) goes to the bottom panel, the rule ROCKNIX uses, so melonDS's, Azahar's and Cemu's
  two-window modes land right.

## Memory

The aim is the smallest idle footprint that still has every piece a handheld needs. What runs while the menu is up:

| Process | Why | Rough resident size |
|---|---|---|
| `banana-ui` | the menu (software-drawn, two full-HD-class canvases) | 60-90 MB |
| `pipewire` + `wireplumber` | sound: the Thor's audio DSP needs its routing set up from a UCM profile, and Bluetooth headphones | 20-30 MB |
| `bananad` | keys, lid, sleep, brightness | 2 MB |
| `udevd`, `dbus-daemon` | modules and firmware as devices appear; PipeWire's and BlueZ's bus | 5 MB |
| `wpa_supplicant`, `udhcpc` | only while Wi-Fi is on | 5 MB |
| `bluetoothd` | only while Bluetooth is on | 4 MB |

These are estimates from the same programs on the RG DS and on a PC, not measurements on a Thor; `free -m` and
`ps` on the handheld will replace them. The total should sit near 150 MB of the Base model's 8 GB.

What keeps it there:

- **No compositor at idle**, and none under RetroArch. sway exists only around a program that needs it.
- **Nothing resident for features not in use**: the browser, Wine, Box64, the Store and file sharing start when asked
  for and leave nothing behind. PC games' runtime isn't even in the image (below).
- **zram.** Compressed swap in memory, half the RAM (4 GB on the Base, 8 GB on the Max; `HW_ZRAM` in `hw.conf`), for
  the heaviest emulators and PC games. LZ4 going out, zstd for pages that stay idle. It costs nothing while unused.
  This and the other memory, CPU, card and sound settings, with what ROCKNIX does and how to measure each on a Thor,
  are in `docs/THOR-TUNING.md`.
- **The sound server stays.** Plain ALSA would save about 25 MB, but the Thor's sound is a DSP graph that PipeWire
  sets up from ROCKNIX's UCM profile, with the jack and Bluetooth codecs handled. That is worth the memory.

## What is in it

| | How | Notes |
|---|---|---|
| Both screens in the menu | `banana-ui` on KMS, each panel its own shape | touch on both |
| Theme | Thor OLED, from the owner's mockup | the other BananaOS themes stay selectable |
| Emulators | RetroArch with 50 cores, and Dolphin, ARMSX2, Azahar, PPSSPP, Vita3K, xemu, Flycast and ScummVM under `run-wayland` | 56 systems; `docs/THOR-EMULATORS.md` has the list and what is left out (Wii U, PS3, Switch, Xbox 360) |
| PC games | Box64 + Wine + DXVK + VKD3D-Proton, and your Steam library | below |
| Home button | the menu over a frozen game and back; held, the quick menu | below |
| First-time setup | language, Wi-Fi, time zone, name, RetroAchievements, home server, Steam, the fan and lights, box art | Settings > System > First-time setup runs it again |
| Tuning | `docs/THOR-TUNING.md` | what ROCKNIX does for this chip and what this image does |
| Sound | PipeWire + WirePlumber over AudioReach (UCM) | speakers, jack, Bluetooth (SBC, Opus; LDAC and aptX where built) |
| Wi-Fi | `wpa_supplicant` (WPA2/WPA3), the WCN7850 on `ath12k` | Settings > Network |
| Bluetooth | BlueZ: pads (classic and LE), headphones through PipeWire | Settings > Network > Bluetooth |
| Web browser | WPE WebKit with Cog | below |
| RomM | `banana-sync`: games, box art and saves from a RomM (or Romhost) server | Settings > Network > Home server; pairing by code |
| Files, file sharing, SSH | Files' servers (SMB, SFTP, FTP), ksmbd, dropbear | as on the RG DS |
| Store, Activity, Bshare, PortMaster | as on the RG DS | |
| Updates | A/B slots from GitHub releases | the Thor's own packages |
| Fan, stick lights, GPU boost | `handheld`, natively on this image: the fan by the SoC's temperature (ROCKNIX's curves), the rings' colour, brightness, battery mode and breathing, the GPU's top clock | Settings > Sleep & power > Fan, lights & speed |
| Performance modes | `banana-perf`'s per-cluster caps | per system and per game |

## PC games

What GameNative and Winlator do on Android, without Android underneath:

- **Box64** (in the image, built for the Snapdragon 8 Gen 2) recompiles x86_64 code for the Thor's cores and passes
  calls into Vulkan, Wayland and ALSA straight to the system's own libraries. Its Box32 side runs 32-bit games: the
  Thor's big cores can't run 32-bit ARM code, so Wine's WoW64 mode is used.
- **Wine** 11.0 (x86_64, WoW64), **DXVK** 3.1.1 (Direct3D 8-11) and **VKD3D-Proton** 3.0.1 (Direct3D 12) are fetched
  once from their projects' releases, with their SHA-256 checked, by **PC games > Set up PC games** (about 100 MB
  down, 1 GB unpacked). They draw through Vulkan on Turnip, Mesa's driver for the Adreno 740.
- Wine needs symbolic links and permissions, which the card's exFAT doesn't have. The runtime and Wine's prefix live
  in one ext4 image file on the card (`pc/pc.ext4`, 6 GB), mounted while a PC game runs.
- **Your games**: copy a game's folder into `roms/pc` on the card. The shelf shows it; its main program is guessed
  (the largest `.exe` that isn't an installer or a helper) and written to a small `<Game>.pc` file you can edit
  (`exe`, `args`, `env`, `d3d = wined3d` for a game DXVK draws wrong).
- **Your Steam library**: sign in to Steam in the first-time setup or in Settings by scanning a QR code with the
  Steam app on your phone (nothing to type), or with the account name, password and Steam Guard code. Your games
  appear on the PC games shelf with Steam's own
  box art; picking one that isn't on the card downloads its Windows build to `roms/pc/<Game>` and starts it. The
  login and the downloads are DepotDownloader's (a native ARM64 program, fetched once with its SHA-256 checked, no
  Wine under it). A login token is kept on the card, never the password. `usr/libexec/banana/steam` is the tool.
- A game that needs Steam itself running (Steam's DRM, its multiplayer, cloud saves) won't start this way. For those
  the shelf's **Steam** entry installs and runs Steam's own Windows client in the prefix. Whether that runs
  acceptably on Box64 on this chip is untested, and it is the part most likely to need work.
- **Box art** for PC games: Steam's library covers by app id; a game copied by hand is looked up by its folder's name
  in Steam's store search; with a SteamGridDB API key (Settings > Accounts) that site is asked for what Steam doesn't
  have.

Expect what the Android apps deliver on the same chip: older and lighter Windows games play well, recent heavy ones
don't. Anti-cheat games won't start.

## The Home button

The button under the bottom screen is the menu's while a game runs:

- **A press** stops the game where it is and shows BananaOS's menu on both screens. Nothing is saved and nothing
  ends: the game's process is frozen. **A press again**, or picking the same game, goes back into it at the same
  frame. Picking another game ends the frozen one first (it gets the signal to write its saves).
- **Held for three seconds**, it opens the quick menu on the bottom screen (brightness, performance mode, the fan,
  the stick lights) with the game's picture kept on the top one. The Home button or B closes it.

The freeze is the kernel's: the game runs in a control group of its own and every process in it stops at once, an
emulator inside the compositor included (a signal to the launcher alone would have left it playing).

It is quick because nothing is started or torn down. The menu never gives the display device away: a game draws
through a DRM lease of the panels (RetroArch on one panel beside the game panel, as on the RG DS; everything else on
both), and the menu, which owns the lease, puts its own picture on the panels and later the game's back. That is one
page flip each way. `home_switch = off` in `banana/settings.conf` gives a game the display device directly, as
before, with the Home button off.

Which button it is comes from the hardware profile (`HW_HOME_KEY` in `etc/banana/hw/thor.conf`, a kernel key code).
It is set to the pad driver's "mode" key; the driver has one other spare key, and if that one turns out to be the
button under the screen the line says what to change it to.

## USB debugging

On this build, a computer plugged into the Thor's USB-C port has complete access to it, with nothing to set up and no
password (`usr/libexec/banana/usb-debug`, started at boot):

- **A network card.** The Thor is `10.55.0.1` and gives the computer an address: `ssh root@10.55.0.1`, `scp`, and
  everything else that runs over a network. Works as it is on Linux, macOS and Windows 11.
- **A serial port** (`/dev/ttyACM0` on Linux, a COM port on Windows; `screen /dev/ttyACM0 115200`): a root shell that
  needs no network, for when the network is what is broken.
- **adb**: `adb shell`, `adb push`, `adb pull`, `adb forward`, as with an Android device.

It is for bringing the Thor up and for working on BananaOS. Anyone with the cable is root, so an image meant for
other people should have it off: take `HW_USB_DEBUG=yes` out of `etc/banana/hw/thor.conf` before building, or put
`usb_debug = off` in `banana/settings.conf` on the card. While it is on, the port still charges; a pad or a drive
plugged in instead works as before where the port switches roles by itself (not tried on a Thor).

## The web browser

WPE WebKit with Cog: one full-screen page on the top screen, no desktop toolkit under it. An on-screen keyboard
(wvkbd) comes up on the bottom screen. It is driven by touch; the keyboard's own layers have the arrows and Alt
(Alt+Left goes back). The start page has an address and search box and bookmarks. It is the largest thing in the
image (WebKit is about 60 MB on disk), and it uses no memory unless opened.

## Building it

```sh
git clone https://github.com/buildroot/buildroot -b 2026.02.3 ../buildroot
make BUILDROOT_DIR=../buildroot thor
```

The first build takes hours (Qt, WebKit, Mesa, the emulators) and wants 100 GB of disk. The image is
`output/thor/images/banana-thor.img.gz`. Writing it to a card and the one-time OpenABL step are in
`docs/THOR-PORT.md`.

## What was checked, and what wasn't

Checked on a PC:

- the defconfig loads in Buildroot 2026.02.3 and keeps every option it sets; the new packages' dependencies resolve;
- Box64's package was cross-compiled for the Thor's CPU with this configuration (an ARM64 `box64` with Box32, built
  by Buildroot from the recipe here);
- `run-wayland` under a headless sway with two outputs: the environment, the exit status, the second-screen rule,
  `--keep-bottom`, SIGTERM (`system/tests/run-wayland-test.sh`);
- `pc-games` against stand-ins: downloads refused on a wrong checksum, the unpacking, the prefix's setup calls, the
  game scan and a run's arguments (`system/tests/pc-games-test.sh`); the three real archives were downloaded once to
  record their checksums and layouts;
- `steam` against stand-ins: the login's states (a wrong password, Steam Guard's code wrong then right), the token
  read back, the library's entries, the covers, a download and its program, the logout
  (`system/tests/steam-test.sh`). Steam's store search, its cover addresses and DepotDownloader's release were
  fetched for real; no account was logged in;
- the emulator packages: the configuration resolves with all of them on, every pinned commit exists upstream and
  ROCKNIX's patches apply to their sources. None was compiled;
- the theme at the Thor's screen shapes, in the menu's headless renderer.

Not checked, and the first things to look at on a Thor:

- the whole image has never been built end to end, and never booted;
- the panels' rotation and the touchscreens' mapping under sway (`HW_WL_ROTATE`, `HW_WL_TOUCH_*` in `hw.conf`);
- Wine under Box64 on this kernel and Mesa; Steam's client in Wine;
- a real Steam login, by QR code or by password: the code is read out of what the downloader draws for a terminal
  and the account out of its token store, both written from its source and tried only on stand-ins. The library's
  list comes from Steam's web API with the downloader's token, which was written
  from how those are documented to work and never run against an account. If the list doesn't come,
  `pc/steam/library.tsv` (`<app id><tab><name>` lines) can be written by hand and the rest works from it;
- USB debugging: the gadget's tree is checked against plain folders; whether the Thor's port comes up as a device,
  and each of the three ways in, needs the handheld and a cable;
- the Home button. It needs a display device with leases, which a PC without the panels doesn't give, so none of it
  has run: the lease of both panels to a compositor (sway opening the lease through the preloaded shim), the menu
  drawing over a stopped game, and the game's picture put back. If a standalone emulator doesn't start on the Thor,
  `home_switch = off` is the first thing to try;
- Cog's flags and the on-screen keyboard's placement;
- every standalone emulator's first start (`docs/THOR-EMULATORS.md` marks each one's state);
- the fan's sensor and PWM paths and the rings' sysfs names (`handheld` is tested against a made-up `/sys`).

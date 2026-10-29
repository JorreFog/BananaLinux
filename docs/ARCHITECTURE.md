# How Banana works

## The card

| Where | What | Size |
|---|---|---|
| boot chain | **OpenABL** (arm-efi; already flashed to the RPC's `abl_a`/`abl_b` slots); there is no U-Boot on the card | — |
| partition 1, `BANANA-BOOT` (FAT) | `a/Image` and `a/<board>.dtb` (and `b/` after an update), `extlinux/extlinux.conf` with a label per slot and `DEFAULT` naming the one to boot | 160 MB |
| partition 2, `banana-root-a` (squashfs, LZ4) | the system, read-only | 256 MB (tens used) |
| partition 3, `banana-root-b` | the other slot: empty until the first update writes the new system there | 256 MB |
| partition 4, `BANANA` (exFAT) | made on the first boot from the rest of the card: `roms/`, `bios/`, `saves/`, `states/`, `banana/` (settings, logs, play stats, favourites, `updates/`) | the rest |

The image holds the first three partitions (the empty slot costs nothing compressed), so it is small and quick to
write. `/usr/libexec/banana/firstboot`
moves the GPT's backup header to the end of the card, adds partition 3 and formats it (exFAT, so Windows and macOS
can read and fill it). The root is never written to; nothing on it can be corrupted by pulling the battery.

## Updates

The build makes `banana-<board>-<version>.update.tar` next to the image: `Image`, the device tree, `rootfs.squashfs`,
`VERSION`, `board` and their `sha256sums`. Put in `banana/updates/` on the card (file sharing copies it over Wi-Fi)
it appears in Settings > System > Update. `/usr/libexec/banana/update apply` checks every file's hash, refuses a
package for another handheld, writes the squashfs to the slot not in use (16 MB at a time, the progress on the
screen), reads it back and compares the hash, copies the kernel and device tree to the boot partition's `b/` (or
`a/`), checks those too, and only then rewrites `extlinux.conf`'s `DEFAULT` (a tiny file, renamed over the old one).
The running system is never touched; a failure at any step leaves everything as it was; the new system runs from
the restart. `Previous version` switches `DEFAULT` back to the other slot, so a version that misbehaves is one
restart away from the one before it. Each update is a line in `banana/logs/updates.log`. `tools/qemu-boot.sh`
applies an update in the VM and boots the other slot, reading `extlinux.conf` as OpenABL does.

## Boot

1. OpenABL (in the `abl_a`/`abl_b` slots; the card itself carries no bootloader) reads the FAT partition's
   `extlinux/extlinux.conf` and starts the `DEFAULT` slot's kernel with `root=PARTLABEL=banana-root-a` (or
   `-b`) `rootfstype=squashfs ro rootwait quiet`. There is no initramfs: the kernel mounts the root itself.
2. Everything the menu needs before the panel is a module is built into the kernel: the one DSI panel (ROCKNIX's
   driver for the CH13726A, its `rotation = <90>`), the buttons (ROCKNIX's `rsinput` on the MCU), the touchscreen
   (FT5452), the WCD937x codec and the two AW88166 amplifiers, and the battery, charger and fan PWM (the PMIC and
   `qcom_battmgr`). Wi-Fi (`ath10k_snoc`) and the GPU (`msm`, Adreno a613 through freedreno) are modules, loaded by
   rcS after the menu is up (there is no udev; bananad loads the drivers USB devices ask for, an Xbox pad's xpad,
   from their uevents' MODALIAS, as udev would). No fbcon, no VTs, no boot logo.
3. busybox init runs `/etc/init.d/rcS`: mount `/proc`, `/sys`, `/run`, `/tmp`, the data partition; load the audio
   stack (sc8280xp/q6apm/LPASS macros/SoundWire, WCD937x, AW88xx) and the fan; start `bananad`; set the menu's
   clocks. Wi-Fi, the clock and the rest go to the background.
4. init starts `banana-ui`. It opens the panel and plays the boot animation from its first frame while another
   thread reads the library and builds the menu; when both are done it dissolves into the menu (four steps of an
   ordered dither in the animation's own pixel size). The animation is one screen (the panel's), so there is no
   second panel and no console/cartridge: the pixel bunch is at rest, BANANAOS types itself in, a shine runs over
   the bananas and LOADING ticks until the menu takes over, with the intro's cue as the wordmark finishes. The sound
   device is up well before that (docs/BOOT-SOUND.md). `splash = off` in settings.conf goes straight to the menu.

What is worth measuring on the device: OpenABL to kernel, kernel to init (the MMC probe and `rootwait`), and init
to the menu's first frame. The kernel's `initcall_debug` and banana-ui's log (it logs the library scan's time)
give the last two.

## The menu: banana-ui

ROCKNIXDS Pixel is drawn by an engine that knows nothing about EmulationStation (`ui/rnds`, imported unchanged
from ROCKNIXDS by `tools/import-rnds.sh`): a *Source* gives it systems, games and status, a *Backend* draws its
textures, and it rasterizes everything else itself (CSS-like boxes, Pixelify Sans text, fitted pictures). In
ROCKNIXDS, ES is the Source and its GL renderer the Backend. In Banana:

- **The panel** (`src/kms.cpp`): the RPC has **one** 1240x1080 AMOLED panel, mounted turned (the device tree's
  `rotation = <90>`), so `Display::openSingle()` opens one connector and `Display::turn()` turns the composed
  screen into the scanout buffer a panel row at a time (a 4x4 register transpose for the quarter turns). `lease()`
  is refused on a one-panel display, so a game takes the whole screen instead of being handed a panel. KMS dumb
  buffers, two per panel, page flips on vblank; the screens are composed in cached memory and only the rectangle
  that changed is copied into the (uncached) scanout buffer.
- **Source** (`src/library.cpp`): the systems in `/etc/banana/systems.conf` (or the copy in `/data/banana/`),
  their icons and colours from the theme's `systems.cfg`, the games found in each system's folder, names and art
  from an EmulationStation `gamelist.xml` when one is there (a card scraped under another firmware keeps its art) or
  from `media/box|cart|label/<rom name>.png`, and Banana's own play counts, play time and favourites.
- **Backend** (`src/soft.cpp`): software, on the CPU. Each screen's draw calls are recorded and compared with the
  last frame's, draw by draw, each with the rectangle it can touch (its clip resolved), in order: a screen whose
  list didn't change keeps its pixels (most animations move one screen), and in one that changed only the
  rectangles of the draws that came, went or moved are cleared to black and drawn again, every draw that touches
  them clipped to them (a moving cursor redraws the cursor, not the screen). Drawing is mostly copies: each
  texture's rows are stored as runs of transparent, opaque and blended pixels, so opaque runs are `memcpy`,
  transparent runs are skipped and only edges are blended — and the blend is done four pixels at a time. A texture
  drawn at another size (the shelf's tile pictures, the system icons) keeps its samples for each size and sub-pixel
  phase it is drawn at again, so a tile at rest or moving by whole pixels is copied, not resampled. The frames are
  the same, pixel for pixel, as drawing every screen from black. In the benchmark (`bench` in a headless script,
  which also says how much of the screens was drawn again) an idle frame costs well under a millisecond on a PC.
- **Pictures** (`src/main.cpp`'s Worker): box art is decoded and fitted off the main thread, by up to three
  niced worker threads (one fewer than the cores) that never hold the lock the frames need, so scrolling through
  a big library doesn't wait for a picture. A box art makes several pictures (a cartridge and a cover; Carts'
  three sizes), so the last few decoded files are kept and each PNG is decoded once.
- **Main loop** (`src/main.cpp`): `poll()` on the buttons, the touchscreen, the DRM device (flip events), the
  picture workers and signals, with a timeout from the engine's next animation step or the next minute (the
  clock). Nothing runs while nothing changes.

The RPC has one screen, so the layout is the DS's 640-wide one at the panel's scale: the canvas is 640 wide and as
tall as the panel allows (640x557 at 1.9375). `themes/pocket.json` is the single-screen theme (status bar, the
shelf of systems at home, the game list in the library, a launch card that drops the game's art into a console slot
as a cartridge -- the insert the boot animation used to end on); `src/settings.cpp` draws the setting's
description and help in the strip under the list where a two-screen handheld put them on the second panel.

**Settings** (`src/settings.cpp`, Start): drawn with the engine's own box styles and Pixelify Sans through the
same Backend. `src/settings_defaults.cpp` is the schema: the keys and accepted values of `banana/settings.conf`,
including the per-system `core.<system>` selections and RetroArch's audio latency and VSync. At startup the card's
file is made whole (missing keys added from `/etc/banana/settings.conf`, invalid values put back, comments and the
user's own keys kept) and only rewritten when something changed. Each change is then written at once to the file
that owns it: `banana/settings.conf` (the menu; a theme change rebuilds the engine in the other palette),
`banana/bananad.conf` (then SIGHUP to bananad), the backlights, and RetroArch's own `retroarch.cfg` for the keys
the menu shows. The time zone is a POSIX `TZ` rule in `settings.conf` (glibc reads those with no zoneinfo files on
the root): banana-ui sets `TZ` for itself and every game it starts, and bananad takes it for its logs on SIGHUP.

**First-time setup**: with no `banana/setup-done` on the card, the menu opens the setup after the boot animation:
five steps on the settings' own pages (Wi-Fi through the network chooser, the time zone, RetroAchievements and
ScreenScraper through the keyboard, Finish). The accounts go to `banana/accounts.conf` (`ra_user`, `ra_password`,
`ss_user`, `ss_password`); `run-retroarch` writes the RetroAchievements one into RetroArch's `cheevos_*`. Skipping
writes `setup-done` too; Settings > System > First-time setup runs it again.

**Wi-Fi** (Settings > Network): the networks around from `wifi scan` (wpa_cli's scan results, one row per name,
strongest first, with its signal, a lock, *Saved* or *Connected*), run on a thread so the menu never waits. A on a
network with a password opens a keyboard: four rows of ten keys in three layers and a row of wide keys, driven by
the d-pad (A types, B deletes, X capitals, Y space, START joins) or by touch on the panel. Joining writes the
network's block first in `banana/wifi.conf` (WPA3-only networks get `key_mgmt=SAE`; a block for the same name is
replaced), restarts wpa_supplicant and follows `wifi status` until it has an address. udhcpc is started by
wpa_cli's action script only once wpa_supplicant is connected, so a handheld with no network in reach isn't kept
awake asking for an address. Company (802.1X) and WEP networks are listed but go in `wifi.conf` by hand. The radio
is the WCN3950 through `ath10k_snoc`.

**File sharing** (Settings > Network): the card's data partition as `\\<address>\banana` over SMB3, served by the
kernel (`ksmbd`, a module loaded only when sharing is switched on) and ksmbd-tools' `ksmbd.mountd`, as a guest
share forced to root (exFAT has no owners) with DOS attributes not stored (exFAT has no extended attributes). No
password, so it is off by default and the settings screen says so. banana-ui watches the ROM folders (inotify):
games copied in are read into the library once the copying has been quiet for 2 s, while the menu shows its home
screen.

**SSH** (Settings > Network, off by default): dropbear with keys only, as root. The keys are written in
`banana/ssh/authorized_keys` on the card and copied to `/run/banana/ssh` (mode 0600, which exFAT can't hold;
`/root/.ssh` points there); the host key is made once per card. Logs and `sleep_debug`'s timings can be read
on a computer without taking the card out.

**Low battery**: at 20% (the LED turns red), 10% and 5% (it blinks) on the battery, a notice in the bubble style
over the bottom of the panel for 8 s, the status bar's battery three times its size; any button hides it.
Each step shows once until the charger has been on, and a step passed during a game shows when the menu is back.
banana-ui reads the battery with the clock (once a minute), after a game and after a wake; bananad does the
shutting down. The notice is drawn by the backend at the end of the screen's draw list, so it shows over the
settings too and costs nothing while it doesn't change.

Starting a game: banana-ui writes the game's pid to `/run/banana/game.pid`, closes the display, the input
devices and the sound device, sets the system's clock profile, and runs its `run` command. The game then runs under
**cage**, a small kiosk Wayland compositor (`cage`, MIT): one application, fullscreen, DRM master on the panel. It
turns the output with the panel's "panel orientation" (wlroots reads the same connector property banana-ui reads),
so the game, RetroArch's own menu, its OSD and its widgets all come out upright from one place, instead of a
rotation patch per RetroArch video driver. The menu keeps drawing straight on KMS -- cage is started for a game and
let go when it exits -- so the menu still costs no compositor. When the game exits banana-ui takes the panel back,
records the play, and shows the game's library again.

## Sleep: bananad

`bananad` reads every input device next to the menu and the games (evdev allows several readers; it grabs
nothing). The RPC is a **slate with no lid**: its switch devices are the power key (no switches at all) and the
codec's jack (headphone, microphone, line-out, charging — never `SW_LID`), which bananad detects by the *list* a
device advertises (`EVIOCGBIT(EV_SW)`), not its state. So a short press of power sleeps it, and:
the idle timer **dims the panel after two minutes but never suspends** unless `autosleep_minutes` has been asked
for (the image's own default does not count as a wish; only the card's `bananad.conf` does). When it is asked for,
the menu's idle minutes put it to sleep:

1. The game's process group gets `SIGSTOP` (a dynarec must not run on while the clocks drop).
2. `/etc/banana/sleep.d/*` run with `pre` (Wi-Fi down and its driver unloaded; nothing at all when Wi-Fi is off).
3. `sync()`: this kernel has `SUSPEND_SKIP_SYNC`, so the flush is bananad's (with saves written within 2 s anyway,
   `vm.dirty_expire`).
4. The RTC alarm for the battery check (`sleep_check_hours`, 4), devices set to resume in parallel
   (`/sys/power/pm_async`, `fast_resume`), `deep` in `/sys/power/mem_sleep`, and `mem` to `/sys/power/state`: the
   SoC powers down, DRAM self-refreshes.

**There is no RTC alarm on the RPC to wake it with.** Its PMIC's RTC is marked `qcom,no-alarm` in the device tree --
the alarm interrupt belongs to the ADSP, not the application processor -- so the RTC has no `wakealarm` attribute at
all, `rtc_alarm()` cannot arm anything, and the "battery checked" wake in `banana/logs/sleep.log` never happens: on
this handheld `sleep_check_hours` does nothing, and the power key (or a charger on the USB-C port) is the only way
out of `deep`. Everything else about a sleep is as described.

The device tree makes the power key's PMIC a wakeup source. When `/sys/power/state` returns:

- **The battery check's alarm** (the RTC clears `wakealarm` when it fires): at `sleep_poweroff_percent` (5%) or
  under, the game gets `SIGCONT` and then `SIGTERM` to write its saves, and the handheld shuts down; otherwise
  straight back to sleep, nothing else run.
- **The power button**: the game gets `SIGCONT` and banana-ui `SIGUSR1` (a redraw: the kernel's DRM resume has
  already put our buffers back on the panel, so no modeset), all within a fraction of a millisecond of the kernel
  returning; then the `post` hooks (Wi-Fi back) in the background, the LEDs, the headphone route; the press that
  woke it is ignored.

If a suspend fails, bananad goes on more carefully by itself: devices one at a time, then `s2idle`; if even that
fails, the backlight goes off until the next press. Every sleep is a line in `banana/logs/sleep.log` (time to
suspend, time asleep, why it woke, how long the game and the menu took to come back); with `sleep_debug = yes` the
kernel's per-device times go to `banana/logs/sleep-devices.log`, slowest first.

What a wake costs is then the kernel's device resume: on the RPC mostly the panel's power-on sequence and the SD
card. `system/bananad/tests/sleep-test.sh` runs all of the above against a made-up `/sys` in its own namespaces (a
FIFO for `/sys/power/state`, real input events for the power key).

Two idle minutes in the menu dim the panel (`dim_minutes`, `dim_percent`); the first press brings it back, and a
game is never dimmed. bananad itself sleeps in `poll()` until something is due (the battery
look once a minute, the dim or sleep deadline, a pending save), so an idle handheld wakes it about once a minute.
Hold power 2 s to shut down; the volume keys change the two amplifiers' level -- the system's volume -- wherever they
are pressed, with the menu's own indicator (the pill at the top of the panel) and deliberately none over a game
(banana-ui is not drawing while `cage` owns the panel, and RetroArch's message box is a different look); Settings >
Volume sets the same control, saved on the card; F +
volume the backlight; a battery at 3% and
discharging shuts down cleanly. The fan is the kernel's own (`pwm_fan` off the PMIC's PWM, following the
temperature; bananad does not touch it). Settings:
`/etc/banana/bananad.conf`, overridden by `/data/banana/bananad.conf` (the settings screen writes it and sends
SIGHUP).

## What it costs

Measured in the VM (`tools/qemu-boot.sh`, a 1 GB machine, idle in the menu): the menu draws only when something
changes and sleeps in `poll()` otherwise; bananad wakes about once a minute; no sound server, no udev, no
compositor (the menu is drawn straight on KMS -- cage exists only while a game runs), no shell waiting for input.
`tools/qemu-boot.sh` measures all of this from inside the VM over SSH
(`resources-*.txt`: memory, each program's own, the CPUs' idle share).

The menu's own CPU cost, headless on a PC, is in `docs/PERFORMANCE.md`: `make bench` builds a 600-game library and
prints the library scan, the menu's start-up and the first frame's render at both canvas sizes, plus the idle and
scrolling frame cost. It is the host baseline to compare a change against (the VM's is the memory above).

The kernel is built for this handheld only: from ROCKNIX's SM4450 7.2 config, Banana's fragment leaves out what
the device tree doesn't have (HDMI, cameras, Bluetooth, FUSE, swap, netsched), so the kernel in memory is smaller
and boot probes fewer drivers; the modules that remain are the Wi-Fi and GPU ones, the audio stack, the fan, file
sharing and virtio for QEMU. Wi-Fi's scans back off to one every five minutes when no known network is in reach.
Buildroot builds everything with `-O2` for the Cortex-A55/A78, without the stack protector in every function and
without eager symbol binding at start: a single-user game device gets the cycles instead. RetroArch runs with two
swapchain images and nothing behind the game (no rewind buffer, no run-ahead, no shader pass, no save-state
thumbnails).

## The rest of the hardware

**Panel.** One 1240x1080 AMOLED DSI panel (the CH13726A), its timing in the device tree; the device tree's
`rotation = <90>` says the panel is turned in the handheld, so `banana.rotate=0|90|180|270` on the kernel command
line can override it. Its backlight is a PWM.

**GPU.** The Adreno a613 through Mesa's freedreno (A6xx), `msm` and the a6xx firmware. The kernel's catalog and
Mesa both have to know the GPU's id (613); both carry it. The module is loaded by rcS after the menu's first
frame.

**LEDs.** The power/status/charging LEDs (PWM), set by bananad only when the battery's state changes, which the
kernel announces as power_supply uevents (a netlink socket; there is no udev): the charging LED follows the
charger's own LED trigger, the critical blink is the kernel's timer trigger.

**Sound.** No sound server: ALSA's default device is written by bananad to `/run/banana/asound.conf`. The card is
the WCD937x codec with the two AW88166 amplifiers on I2S, driven by the Qualcomm **AudioReach** DSP (the
`sc8280xp`/q6apm machine stack). Three things decide whether audio is heard: the route must be written *before* a
frontend is opened (the DSP instantiates the graph at the open and takes the mixer values it finds then), only the
first frontend produces sound (so the menu and a game hand it over), and there is no UCM profile (so routing is by
`amixer`, not the `hooks` plugin). bananad publishes the route it chose (speakers or headphones) to
`/run/banana/audio-route`; banana-ui and `run-retroarch` both write those values and fall back to "speakers on".
The menu's stream lives exactly as long as the screen is on: a cue then costs nothing, no DSP graph to instantiate
(`Sound::setScreenOn()`, which the burn-in guard's blank calls -- the screen being off *is* the idle -- and its wake
undoes, pre-opening the device while the press that woke the screen is swallowed). It is given up for a game
(`Sound::release()`) and pre-opened again when the game ends. Both sides wait one beat only after such a handover.
The route and its read-back are the ALSA mixer API, not `amixer` (forking four or five processes per open was most
of the delay before the menu's first cue), asound.conf's own change being what makes it re-open the mixer.

**Volume** is the two AW88166 amplifiers' own gain, not the DSP's per-stream volume: a frontend's graph latches the
mixers when it is instantiated, so the stream volume cannot be changed under a running game, while the amp's gain
takes effect at once. ROCKNIX's kernel patch gives the codec's `PCM Playback Volume` a real dB scale and this kernel
exposes the two amps as `Left PCM` / `Right PCM` (`0` mute … `511` 0 dB, the loudest). `/usr/libexec/banana/volume`
drives both in amixer's own percent, and bananad publishes them (as `PCM`) in `/run/banana/mixer` for the volume keys
and Settings > Volume. The keys drive that one control wherever they are pressed -- the menu's OSD draws the level,
and a game shows none (banana-ui is not drawing while `cage` owns the panel, and RetroArch's own message box is a
different look). RetroArch's software gain is pinned to unity. The level lives in `/data/banana/asound.state` (alsactl),
replays at boot, and is not pinned again by rcS, bananad or `run-retroarch` once the card has one.

**Clocks: banana-perf.** The RPC is two clusters — `policy0` (six Cortex-A55) and `policy6` (two Cortex-A78) — so
every profile sets *every* `policy*`, not just the first:

| Profile | CPU | For |
|---|---|---|
| `ui` | schedutil, ~1.4 GHz | the menu |
| `powersave` | schedutil | 8-bit and 16-bit systems |
| `balanced` | schedutil, full range | GBA, SNES, PlayStation |
| `performance` | performance | N64, ports |

## Booting it without the handheld

`tools/qemu-boot.sh` boots a card image on QEMU's `virt` machine with the same kernel and root: the card on a PCI
SDHCI (so it is `mmcblk0`, and the first boot partitions it as on the handheld), one 1240x1080 panel on a virtio GPU
(`Virtual-1`), a virtio keyboard and tablet for the buttons and the touchscreen. Kernel parameters make the image
fit a display other than the handheld's: `banana.console=1` sends rcS's milestones and banana-ui's log to the
console (the serial port here, the UART on the handheld), `banana.top=` names the panel's connector and
`banana.touch=` the touchscreen's device. The test script drives the menu through QEMU's monitor (`sendkey`,
`screendump`) and reads the card's partition afterwards. The CI's `boot` job runs it on every image it builds and
keeps the screenshots and logs as an artifact. What it can't test: the panel's DSI timing and its rotation, sleep
(QEMU's `virt` has no suspend to RAM), sound, Wi-Fi, the GPU.

## Hardware support

`buildroot/board/retroid-pocket-classic` is ROCKNIX's SM4450 support at a pinned commit: its kernel patches that
touch what the RPC has (in ROCKNIX's order; the ones for other devices' device trees, panels and drivers are left
out), the Retroid Pocket Classic device trees, its kernel config (Banana's changes are in `linux-banana.fragment`),
the audio topology and firmware, and the OpenABL boot chain. A newer ROCKNIX is a diff.

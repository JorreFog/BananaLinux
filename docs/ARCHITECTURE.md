# How Banana works

## The card

| Where | What | Size |
|---|---|---|
| sector 64 (32 KiB) | U-Boot for the RK3568 (`u-boot-rockchip.bin`: Rockchip's DRAM init, U-Boot SPL, TF-A BL31, U-Boot) | ~1.5 MB |
| partition 1, `BANANA-BOOT` (FAT) | `a/Image` and `a/<handheld>.dtb` (and `b/` after an update), `extlinux/extlinux.conf` with a label per slot and `DEFAULT` naming the one to boot | 64 MB |
| partition 2, `banana-root-a` (squashfs, LZ4) | the system, read-only | 256 MB (tens used) |
| partition 3, `banana-root-b` | the other slot: empty until the first update writes the new system there | 256 MB |
| partition 4, `BANANA` (exFAT) | made on the first boot from the rest of the card: `roms/`, `bios/`, `saves/`, `states/`, `banana/` (settings, logs, play stats, favourites, `updates/`, Downloads' `downloads/` and `nas.conf`), `drastic/`, `superdrastic/` | the rest |

The image holds the first three partitions (the empty slot costs nothing compressed), so it is small and quick to
write. `/usr/libexec/banana/firstboot`
moves the GPT's backup header to the end of the card, adds partition 3 and formats it (exFAT, so Windows and macOS
can read and fill it). The root is never written to; nothing on it can be corrupted by pulling the battery.

## Updates

The build makes `banana-<board>-<version>.update.tar` next to the image: `Image`, the device tree, `rootfs.squashfs`,
`VERSION`, `board` and their `sha256sums`. Put in `banana/updates/` on the card (file sharing copies it over Wi-Fi)
it appears in Settings > System > Update. `/usr/libexec/banana/update apply` checks every file's hash, refuses a
package for the other handheld, writes the squashfs to the slot not in use (16 MB at a time, the progress on the
screen), reads it back and compares the hash, copies the kernel and device tree to the boot partition's `b/` (or
`a/`), checks those too, and only then rewrites `extlinux.conf`'s `DEFAULT` (a tiny file, renamed over the old one).
The running system is never touched; a failure at any step leaves everything as it was; the new system runs from
the restart. `Previous version` switches `DEFAULT` back to the other slot, so a version that misbehaves is one
restart away from the one before it. Each update is a line in `banana/logs/updates.log`, and the new device tree
is retuned by `banana-panel-tune` on its first boot. `tools/qemu-boot.sh` applies an update in the VM and boots
the other slot, reading `extlinux.conf` as U-Boot does.

## Boot

1. The boot ROM loads U-Boot from sector 64. U-Boot is built with no boot delay, no environment and only the SD card
   as a target (ROCKNIX's Quartz64-based configuration for these handhelds, `board/rgds/uboot-*`).
2. U-Boot reads `extlinux.conf` and starts the `DEFAULT` slot's kernel with `root=PARTLABEL=banana-root-a` (or
   `-b`) `rootfstype=squashfs ro rootwait quiet`. There is no initramfs: the kernel mounts the root itself.
3. Everything the menu needs is built into the kernel: the two DSI panels (the generic DSI panel driver), the
   buttons (ROCKNIX's ADC joypad driver), the touchscreens, the RK817 codec and the speaker amplifiers, the battery.
   Wi-Fi and Panfrost are modules, loaded by rcS after the menu is up (there is no udev; bananad loads the
   drivers USB devices ask for, an Xbox pad's xpad, from their uevents' MODALIAS, as udev would). No fbcon, no VTs, no boot logo.
4. busybox init runs `/etc/init.d/rcS`: mount `/proc`, `/sys`, `/run`, `/tmp`, the data partition; start
   `bananad`; set the menu's clocks. Wi-Fi, the clock and the rest go to the background.
5. init starts `banana-ui`. It opens the panels and plays the boot animation from its first frame while another
   thread reads the library and builds the menu; when both are done it dissolves into the menu (four steps of an
   ordered dither in the animation's own pixel size). `splash = off` in settings.conf goes straight to the menu.

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
  last frame's, draw by draw, each with the rectangle it can touch (its clip resolved), in order: a screen whose
  list didn't change keeps its pixels (most animations move one screen), and in one that changed only the
  rectangles of the draws that came, went or moved are cleared to black and drawn again, every draw that touches
  them clipped to them (a moving cursor redraws the cursor, not the screen). Drawing is mostly copies: each
  texture's rows are stored as runs of transparent, opaque and blended pixels, so opaque runs are `memcpy`,
  transparent runs are skipped and only edges are blended. A texture drawn at another size (the shelf's tile
  pictures, the system icons) keeps its samples for each size and sub-pixel phase it is drawn at again, so a tile
  at rest or moving by whole pixels is copied, not resampled. The frames are the same, pixel for pixel, as drawing
  every screen from black. In the benchmark (`bench` in a headless script, which also says how much of the
  screens was drawn again) an idle frame costs well under a millisecond on a PC.
- **Display** (`src/kms.cpp`): KMS dumb buffers, two per panel, page flips on vblank. The screens are composed in
  cached memory and only the rectangle that changed is copied into the (uncached) scanout buffer.
- **Pictures** (`src/main.cpp`'s Worker): box art is decoded and fitted off the main thread, by up to three
  niced worker threads (one fewer than the cores) that never hold the lock the frames need, so scrolling through
  a big library doesn't wait for a picture. A box art makes several pictures (a cartridge and a cover; Carts'
  three sizes), so the last few decoded files are kept and each PNG is decoded once.
- **Main loop** (`src/main.cpp`): `poll()` on the buttons, the touchscreen, the DRM device (flip events), the
  picture workers and signals, with a timeout from the engine's next animation step or the next minute (the
  clock). Nothing runs while nothing changes.

**Settings** (`src/settings.cpp`, Start): drawn with the engine's own box styles and Pixelify Sans through the
same Backend. `src/settings_defaults.cpp` is the schema: the keys and accepted values of `banana/settings.conf`,
including the per-system `core.<system>` selections and RetroArch's audio latency and VSync. At startup the card's
file is made whole (missing keys added from `/etc/banana/settings.conf`, invalid values put back, comments and the
user's own keys kept) and only rewritten when something changed. Each change is then written at once to the file that
owns it: `banana/settings.conf` (the menu; a theme
change rebuilds the engine in the other palette), `banana/bananad.conf` (then SIGHUP to bananad), 
`banana/superdrastic.conf` (read by run-drastic), DraStic's `drastic.cfg`, and the backlights. The time zone is a POSIX `TZ` rule in `settings.conf` (glibc reads those with no
zoneinfo files on the root): banana-ui sets `TZ` for itself and every game it starts, so a DS game's clock is local
too, and bananad takes it for its logs on SIGHUP.

**First-time setup**: with no `banana/setup-done` on the card, the menu opens the setup after the boot animation:
five steps on the settings' own pages (Wi-Fi through the network chooser, the time zone, RetroAchievements and
ScreenScraper through the keyboard, Finish). The accounts go to `banana/accounts.conf` (`ra_user`, `ra_password`,
`ss_user`, `ss_password`); run-drastic hands the RetroAchievements one to SuperDrastic (`DSFLIP_RA_*`) and
run-retroarch writes it into RetroArch's `cheevos_*`. Skipping writes `setup-done` too; Settings > System >
First-time setup runs it again.

**Wi-Fi** (Settings > Network): the networks around from `wifi scan` (wpa_cli's scan results, one row per name,
strongest first, with its signal, a lock, *Saved* or *Connected*), run on a thread so the menu never waits. A on a
network with a password opens a keyboard: four rows of ten keys in three layers and a row of wide keys, driven by
the d-pad (A types, B deletes, X capitals, Y space, START joins) or by touch on the bottom panel. Joining writes the
network's block first in `banana/wifi.conf` (WPA3-only networks get `key_mgmt=SAE`; a block for the same name is
replaced), restarts wpa_supplicant and follows `wifi status` until it has an address. udhcpc is started by
wpa_cli's action script only once wpa_supplicant is connected, so a handheld with no network in reach isn't kept
awake asking for an address. Company (802.1X) and WEP networks are listed but go in `wifi.conf` by hand.

**File sharing** (Settings > Network): the card's data partition as `\\<address>\banana` over SMB3, served by
the kernel (`ksmbd`, a module loaded only when sharing is switched on) and ksmbd-tools' `ksmbd.mountd`, as a guest
share forced to root (exFAT has no owners) with DOS attributes not stored (exFAT has no extended attributes). No
password, so it is off by default and the settings screen says so. banana-ui watches the ROM folders (inotify):
games copied in are read into the library once the copying has been quiet for 2 s, while the menu shows its home
screen.

**SSH** (Settings > Network, off by default): dropbear with keys only, as root. The keys are written in
`banana/ssh/authorized_keys` on the card and copied to `/run/banana/ssh` (mode 0600, which exFAT can't hold;
`/root/.ssh` points there); the host key is made once per card. Logs and `sleep_debug`'s timings can be read
on a computer without taking the card out.

**Low battery**: at 20% (the LED turns red), 10% and 5% (it blinks) on the battery, a notice in the bubble style
over the bottom of the top panel for 8 s, the status bar's battery three times its size; any button hides it.
Each step shows once until the charger has been on, and a step passed during a game shows when the menu is back.
banana-ui reads the battery with the clock (once a minute), after a game and after a wake; bananad does the
shutting down. The notice is drawn by the backend at the end of the top screen's draw list, so it shows over the
settings too and costs nothing while it doesn't change.

Starting a game: banana-ui writes the game's pid to `/run/banana/game.pid`, closes the display, the input
devices and the sound device, sets the system's clock profile, and runs its `run` command. When the game exits it
takes the panels back, records the play, and shows the game's library again.

## Downloads: banana-fetch

The shelf's Downloads tile (systems.conf's `[downloads]`, `run = banana:downloads`: one of the menu's own pages, not
a program) gets games from a NAS or a computer and unpacks archives. Its pages
are the settings' (`src/settings_downloads.cpp`, the list on the bottom panel, the row in hand explained on the
top): the job in hand, the servers, Add a server, Archives on the card and Options. A server's folders are browsed
(A opens, Y marks, X downloads what is marked, a folder with all in it); the Download page then chooses the card (the
RG DS Plus's second SD card too, each with its free space), the folder (every system of systems.conf with a folder on
the card, BIOS, roms: the one the files belong to by their extension, an archive's looked through, else by the
server's folder names; the folder chosen for a server's folder is remembered) and whether archives are unpacked and
deleted after (`downloads_*` in settings.conf: ask, always, never). The status page shows the job in hand: B leaves
it working, X stops it (its parts and the job kept: A goes on from there), Y cancels it (the job deleted with what
its unfinished downloads left; the files it finished stay). Leaving it (B, the job cleared or cancelled) goes back
where it was opened: the folder the job was started in, on the server or the card, as it was (the cursor on what was
chosen), or home from the job's row there.

banana-ui has no network code for it: `banana-fetch` (`src/fetch/`) does it, in two shapes.

- `banana-fetch serve`, while the pages are open: a line protocol on its stdin and stdout (tab-separated fields),
  one request at a time, its answer read on a thread (which wakes the menu's loop when it is in): log in, list a
  folder, walk the folders chosen into the files to download and where each goes, look into an archive, add or
  change a server. A listing or a walk that finds the connection gone (an idle NAS hung up, Wi-Fi slept) connects
  again once. It goes when the pages close, or after 10 idle minutes.
- `banana-fetch run`, the jobs: `banana/downloads/<time>.job` on the card (the server, the files with their sizes and
  where each goes; unpacking, the archive deleted after), done one after the other in the background while the menu
  shows anything else. What happened to each file is a line of `<job>.done`, so a job cut short (stopped, the
  battery, a crash) goes on from there when A resumes it, and a file half downloaded goes on from its `.part`. Its
  pid is `/run/banana/transfer.pid`, its progress a line in `/run/banana/fetch/status` (twice a second at most, in a
  folder of its own so that the menu's watch on `/run/banana` isn't woken by it), which the pages read. A job done
  becomes `<time>.finished` and is told in `/run/banana/fetch.finished`: the menu then says the finished jobs it
  hasn't seen over the top panel, whatever it shows. It runs niced, with idle I/O priority, and banana-ui stops it
  (SIGSTOP to its process group, 7zz with it) while a game runs, and lets it go on after the game, or when the menu
  starts again (one that went away during a game).

Protocols: SMB 2 and 3 (libsmb2: a NAS's shares, Windows' and macOS's file sharing; a server's shares are its
root; a guest logs in over SMB 3.0.2, or SMB 2 on an older server, as libsmb2 6.2 signs a 3.1.1 guest's requests with
a key the server doesn't have), SFTP (libssh2: a password, or keyboard-interactive; the server's key trusted the
first time and checked after), FTP and FTPS (libcurl: passive, explicit TLS, the certificate trusted the first time
and checked in OpenSSL's own verification, before the password is sent; MLSD, or LIST where a server has no MLSD;
names in CP1251 or CP866 for old servers). A file goes into `<file>.part` next to where it belongs and is renamed once
its size is right; a drop (Wi-Fi coming back after a sleep, a server restarting, on any of the protocols) is tried
again after 3, 10, 30, 60 and 60 s from where it stopped, and a connection quiet for 30 s is dropped and tried again.
What another try won't change (the login refused, a key that changed, a file that isn't there or isn't allowed, a
full card) fails the file at once, and the job goes on with the next. The room needed and FAT32's 4 GB a file (the
second card may be FAT32) are checked before anything is written.

Archives: zip natively (stored and deflated, zip64, CRCs checked; names in UTF-8, or in CP866 or CP437 as old Windows
zips have them), 7z, RAR and the rest (an encrypted zip, another method) with 7-Zip's own `7zz` (built from its
source: Buildroot's p7zip has no RAR), on one thread (several take many times the memory). A multi-volume archive
(`.7z.001`, `.partN.rar`, `.rar` with `.r00`...) is unpacked once its last volume is in. Unpacking goes into
`<card>/.banana-unpack/`, on the same card, and its files are moved into place when it worked; an archive with a
path out of it (`..`, an absolute one) is refused whole, macOS's and Windows' clutter (`__MACOSX`, `.DS_Store`,
`Thumbs.db`) and links are left out, names the cards can't take are changed (`:` to `_`; the same name twice gets
` (2)`, and a file never takes the place of a folder of its name, nor a folder a file's), and an archive holding a
single folder is unwrapped. An archive whose dictionary wouldn't fit in the memory free is left packed, as is one
with a password: Archives on the card unpacks it, the password typed there. Zips for a system whose emulator plays
them zipped (the DS, the GBA...) are kept as they are unless asked, each archive judged by itself (the job's
`keep_zips`): the Download page's Unpack archives decides for a choice of zips alone, a row of their own when the
choice may hold 7z or RAR archives too (a folder), which are unpacked all the same.

Servers are in `banana/nas.conf` (sections of key = value, read as data, never run). A password is kept only when
Remember password is on, sealed: AES-256-GCM under a key made (HKDF-SHA256) from `banana/nas.key` (32 random bytes)
and the SoC's serial number (`/proc/device-tree/serial-number`), which isn't on the card, and bound to the server's
protocol, address, port and user. The card read on a computer, or put in another handheld, doesn't give it away
(Downloads asks for it again); someone with the handheld itself can still open it, as there is no secure store. That
rests on the serial number, which U-Boot puts in the device tree: without one, the key is made from `nas.key` alone,
so the card alone opens the passwords, and banana-fetch says so in `banana/logs/downloads.log` when it seals one.
File sharing leaves both files out. A password not kept goes to the runner in a file of `/run/banana` (tmpfs, 0600)
that it reads once and deletes, and an archive's goes to 7zz on its stdin (where 7zz asks for it), not with `-p`:
passwords are never on a command line nor in `banana/logs/downloads.log`.

`ui/tests/fetch-test.sh` checks the archives (7z and RAR test archives, and zips made by Python: CP866 names, a zip
slip, zip64..., and a 7z of 1502 files), the session and the runner (cuts, stalls, `.part`s, a full card, passwords,
a connection lost between listings) against a folder standing in for a server (`BANANA_FETCH_TEST=1`);
`ui/tests/run.sh` goes through the pages headless with it (taps on their buttons too, and a runner at work beside
the menu); and `ui/tests/fetch-servers.sh` runs against real servers in Docker (vsftpd for FTP and FTPS, pure-ftpd
for MLSD, OpenSSH, Samba with a guest share), a download on each protocol cut in the middle by a proxy and resumed.

## Sleep: bananad

`bananad` reads every input device next to the menu and the games (evdev allows several readers; it grabs
nothing). Closing the lid (the hall sensor, `SW_LID`), a short press of power, or 10 idle minutes in the menu put
the handheld to sleep:

1. The game's process group gets `SIGSTOP` (ROCKNIX found DraStic's dynarec must not run on while the clocks drop).
2. `/etc/banana/sleep.d/*` run with `pre` (Wi-Fi down and its driver unloaded: the RTL8821CS doesn't survive a
   suspend otherwise; nothing at all when Wi-Fi is off).
3. `sync()`: this kernel has `SUSPEND_SKIP_SYNC`, so the flush is bananad's (with saves written within 2 s anyway,
   `vm.dirty_expire`).
4. The RTC alarm for the battery check (`sleep_check_hours`, 4), devices set to resume in parallel
   (`/sys/power/pm_async`, `fast_resume`; ROCKNIX's kernel patch turns it off by default), `deep` in
   `/sys/power/mem_sleep`, and `mem` to `/sys/power/state`: TF-A powers the SoC down, DRAM self-refreshes.

The device tree makes the lid (on opening only) and the RK817 PMIC (the power key, the RTC alarm) wakeup sources.
When `/sys/power/state` returns:

- **The battery check's alarm** (the RTC clears `wakealarm` when it fires): at `sleep_poweroff_percent` (5%) or
  under, the game gets `SIGCONT` and then `SIGTERM` to write its saves, and the handheld shuts down; otherwise
  straight back to sleep, nothing else run.
- **Anything with the lid still shut** (the power button pressed in a bag): straight back to sleep.
- **The lid opened, or the power button with the lid open**: the game gets `SIGCONT` and banana-ui `SIGUSR1` (a
  redraw: the kernel's DRM resume has already put our buffers back on the panels, so no modeset), all within a
  fraction of a millisecond of the kernel returning; then the `post` hooks (Wi-Fi back) in the background, the LEDs,
  the headphone route; the press that woke it is ignored.

A Downloads job (banana-fetch's runner, its pid in `/run/banana/transfer.pid`) with no game running holds the
lid's sleep and the idle one: the lid shut makes the panels dark, the job goes on, and the handheld sleeps once it is
done (`sleep.log` says so). The power button sleeps at once, as ever, and the job goes on from where it was after. A
runner that is stopped (`T` in `/proc/<pid>/stat`: banana-ui went away during a game) holds nothing.

If a suspend fails, bananad goes on more carefully by itself: devices one at a time, then `s2idle`; if even that
fails with the lid shut, both backlights go off until it opens. With lid sleep switched off, closing the lid only
turns the backlights off. Every sleep is a line in `banana/logs/sleep.log` (time to suspend, time asleep, why it
woke, how long the game and the menu took to come back); with `sleep_debug = yes` the kernel's per-device times go
to `banana/logs/sleep-devices.log`, slowest first.

What a wake costs is then the kernel's device resume: on the RG DS mostly the two panels' power-on sequences
(ROCKNIX's 20 + 10 + 50 ms, the init commands and 120 ms after sleep-out, one panel after the other) and the SD
card; on the RG DS Plus the panels take about a fifth of that. `system/bananad/tests/sleep-test.sh` runs all of the
above against a made-up `/sys` in its own namespaces (a FIFO for `/sys/power/state`, real input events for the lid).

Two idle minutes in the menu dim both panels to 30% (`dim_minutes`, `dim_percent`); the first press brings them
back, and a game is never dimmed. bananad itself sleeps in `poll()` until something is due (the battery look once a
minute, the dim or sleep deadline, a pending save), so an idle handheld wakes it about once a minute. Hold power
2 s to shut down; volume keys change the mixer, F + volume the backlights; a battery at 3% and discharging shuts
down cleanly. Settings: `/etc/banana/bananad.conf`, overridden by `/data/banana/bananad.conf`
(the settings screen writes it and sends SIGHUP).

## What it costs

Measured in the VM (`tools/qemu-boot.sh`, a 1 GB machine, idle in the menu): 95 MB used in all, the kernel and its
caches included; banana-ui 23.5 MB resident (its scanout buffers and the boot animation's layers in that; 8 MB
headless at 640x480, 12 MB at 1024x768), bananad 1.6 MB, init, getty and the shells 1.3 MB each; the CPUs 99%
idle. The menu draws only when something changes and sleeps in `poll()` otherwise; bananad wakes about once a
minute; no sound server, no udev, no compositor, no shell waiting for input. The kernel is built for these two handhelds
only: from ROCKNIX's RK3566 config, shared by many devices, Banana's fragment leaves out what the two device trees
don't have (other panels, touchscreens and codecs, HDMI, cameras, USB gadget mode, packet filtering, Bluetooth,
FUSE, swap, netsched), so the kernel in memory is smaller and boot probes fewer drivers; fourteen modules remain
(Wi-Fi, the GPU, xpad, file sharing, virtio for QEMU). Wi-Fi's scans back off to one every five minutes when no
known network is in reach. Buildroot builds everything with `-O2` for the Cortex-A55, without the stack protector
in every function and without eager symbol binding at start: a single-user game device gets the cycles instead.
RetroArch runs with two swapchain images and nothing behind the game (no rewind buffer, no run-ahead, no shader
pass, no save-state thumbnails). `tools/qemu-boot.sh` measures all of this from inside the VM over SSH
(`resources-*.txt`: memory, each program's own, the CPUs' idle share).

## The rest of the hardware

**Panels.** Each panel's timing is text in the device tree (the generic DSI driver's `panel_description`). The RG
DS's carries ROCKNIXDS's measured retune (`board/rgds/dts-patches`): front porches 233 and 21 instead of 260 and 10,
60.0013 Hz from the real 42133334 Hz clock instead of 60.10 Hz. `banana-panel-tune` runs once per card after the
first boot: it reads each VOP's real pixel clock (`dclk_vop0/1` in debugfs' clk_summary), looks for the smallest
front porch change within 0.002 Hz of 60 that keeps every number's digit count, and rewrites the boot DTB in place
when that gains at least 0.005 Hz. The RG DS is left as it is; the RG DS Plus goes from 60.17 Hz to 59.999 Hz
(with the clock it asks for, 62.77 MHz; the real one decides). Its report: `banana/panel-tune.txt`.

**GPU.** The Mali-G52 through Mesa's Panfrost (`buildroot/package/mesa3d-panfrost`): Mesa 25.0.7 built with Panfrost
alone, EGL on DRM, OpenGL ES and GBM, and no LLVM. Bifrost's shader compiler is Mesa's own; later Mesa releases
precompile Panfrost's internal shaders from OpenCL C, so Buildroot's mesa3d would build LLVM and clang for the
host and put LLVM on the handheld. The module is loaded by rcS after the menu's first frame.

**LEDs.** `green:power`, `red:status` and `amber:charging` (pwm-leds). bananad sets them only when the battery's
state changes, which the kernel announces as power_supply uevents (a netlink socket; there is no udev): the amber
LED follows the charger's own LED trigger (`<battery>-charging`), the critical blink is the kernel's timer trigger.

**Sound.** No sound server: ALSA's default device is written by bananad to `/run/banana/asound.conf`. RG DS: the
rk817 (card 0) with its `Playback Mux` on HP. RG DS Plus: playback on the aw88166 card, or on the rk817 while its
`Headphones Jack` switch is on; capture always from the rk817 (the microphone). Volume keys: `PCM` on the aw88166,
`Master` on the rk817. A program opened before a change keeps its device until it opens it again. bananad also sets
the rk817's capture gains at boot (`Mic Capture Gain` and `Master Capture Volume`), after `alsactl restore`, so a
state saved by another firmware can't leave the DS's microphone muted; the emulators open `default`'s capture side
unless Settings > Nintendo DS turns the mic off. Playback and capture of one card share its I2S DAI, and
`rockchip-i2s-tdm` marks that DAI `symmetric_rate`: the two streams must run at one rate. bananad therefore pins
both directions of `default` to S16 stereo 48 kHz (the rate the DS emulators already play at) and ALSA's `plug`
resamples the emulators' 44.1 kHz capture to it; opened at 44.1 kHz against a 48 kHz playback the capture's
`hw_params` was refused, and the microphone never started whatever the gains were.

## Clocks: banana-perf

| Profile | CPU | GPU | For |
|---|---|---|---|
| `ui` | schedutil, at most ~1.1 GHz | powersave | the menu |
| `powersave` | schedutil, at most ~1.4 GHz | on demand | 8-bit and 16-bit systems |
| `balanced` | schedutil, full range | on demand | GBA, SNES, PlayStation |
| `performance` | performance | performance | N64, ports |
| `superdrastic` | performance (SuperDrastic's own governor takes over: the lowest clock with no dropped frames, per game) | on demand | Nintendo DS |

## Booting it without the handheld

`tools/qemu-boot.sh` boots a card image on QEMU's `virt` machine with the same kernel and root: the card on a PCI
SDHCI (so it is `mmcblk0`, and the first boot partitions it as on the RG DS), two 640x480 panels on a virtio GPU
(`Virtual-1`, `Virtual-2`), a virtio keyboard and tablet for the buttons and the touchscreen. Three kernel
parameters make the image fit a display other than the RG DS's: `banana.console=1` sends rcS's milestones and
banana-ui's log to the console (the serial port here, the UART on the handheld), `banana.top=` names the top
panel's connector and `banana.touch=` the bottom touchscreen's device. The test script drives the menu through
QEMU's monitor (`sendkey`, `screendump`) and reads the card's partition afterwards. The CI's `boot` job runs it on
every image it builds, for both boards, and keeps the screenshots and logs as an artifact. What it can't test:
the panels' DSI timing, sleep (QEMU's `virt` has no suspend to RAM), sound, Wi-Fi, the GPU (the emulators run
without it, so a DS game only gets as far as its launcher).

## Hardware support

`buildroot/board/rgds` is ROCKNIX's RK3566 support at a pinned commit (`ROCKNIX`), copied by
`tools/sync-rocknix-hw.sh`: those of its kernel patches that touch what the two handhelds have (in ROCKNIX's
order; the ones for other handhelds' device trees, panels and drivers are left out, since the kernel builds every
Rockchip device tree and a patch for another handheld's can assume ROCKNIX patches Banana hasn't), the RG DS and
RG DS Plus device trees, its kernel config (Banana's changes are in `linux-banana.fragment`), the generic DSI panel
and joypad drivers (built in via `external.mk`), the input-polldev patch the joypad driver needs, its U-Boot
patches, and the RK3568 BL31 it boots with. A newer ROCKNIX is one command and a diff.

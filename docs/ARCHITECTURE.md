# How Banana works

## The card

| Where | What | Size |
|---|---|---|
| sector 64 (32 KiB) | U-Boot for the RK3568 (`u-boot-rockchip.bin`: Rockchip's DRAM init, U-Boot SPL, TF-A BL31, U-Boot) | ~1.5 MB |
| partition 1, `BANANA-BOOT` (FAT) | `a/Image` and `a/<handheld>.dtb` (and `b/` after an update), `extlinux/extlinux.conf` with a label per slot and `DEFAULT` naming the one to boot | 64 MB |
| partition 2, `banana-root-a` (squashfs, LZ4) | the system, read-only | 256 MB (tens used) |
| partition 3, `banana-root-b` | the other slot: empty until the first update writes the new system there | 256 MB |
| partition 4, `BANANA` (exFAT) | made on the first boot from the rest of the card: `roms/`, `bios/`, `saves/`, `states/`, `banana/` (settings, logs, play stats, favourites, `updates/`, Files' `downloads/` and `nas.conf`), `drastic/`, `superdrastic/` | the rest |

The image holds the first three partitions (the empty slot costs nothing compressed), so it is small and quick to
write. `/usr/libexec/banana/firstboot`
moves the GPT's backup header to the end of the card, adds partition 3 and formats it (exFAT, so Windows and macOS
can read and fill it). The root is never written to; nothing on it can be corrupted by pulling the battery.

## Updates

The build makes `banana-<board>-<version>.update.tar` next to the image: `Image`, the device tree, `rootfs.squashfs`,
`VERSION`, `board` and their `sha256sums`. Put in `banana/updates/` on the card (file sharing copies it over Wi-Fi)
it appears in Settings > System > Updates > Update. `/usr/libexec/banana/update apply` checks every file's hash, refuses a
package for the other handheld, writes the squashfs to the slot not in use (16 MB at a time, the progress on the
screen), reads it back and compares the hash, copies the kernel and device tree to the boot partition's `b/` (or
`a/`), checks those too, and only then rewrites `extlinux.conf`'s `DEFAULT` (a tiny file, renamed over the old one).
The running system is never touched; a failure at any step leaves everything as it was; the new system runs from
the restart. `Previous version` switches `DEFAULT` back to the other slot, so a version that misbehaves is one
restart away from the one before it. Each update is a line in `banana/logs/updates.log`, and the new device tree
is retuned by `banana-panel-tune` on its first boot. `tools/qemu-boot.sh` applies an update in the VM and boots
the other slot, reading `extlinux.conf` as U-Boot does.

Over the air (#69): the Update page's first row asks GitHub. `update check` reads the release list
(`api.github.com/repos/JorreFog/BananaLinux/releases`, with `curl` and the image's CA certificates), keeps the newest
release that has this board's `banana-<board>-<version>.update.tar` and `SHA256SUMS-<board>.txt` (drafts never;
pre-releases only with Settings > System > Updates > Test versions, on by default since every release so far is one) and
writes the answer to `banana/updates/.check`. GitHub allows 60 unauthenticated requests an hour per address, so the
list is fetched at most once in 5 minutes (`.feed.json`, asked for again with its ETag). `update download` runs on
its own (the page polls `/run/banana/update-download`, B leaves it running, A cancels): it checks the card has room
for the package twice over (apply unpacks it beside itself), fetches the SHA256SUMS first, downloads into
`banana/updates/.partial` (`curl -C -` carries on from a cancelled or broken download), checks the SHA-256 and only
then moves the package into `banana/updates/`, where `update apply` installs it as any copied package. With Settings >
System > Updates > Update check on Daily, the Wi-Fi script runs `update check --daily` when it connects (once a day at most;
nothing is downloaded). `ui/tests/ota.sh` runs all of it against a stand-in server.

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

- **Source** (`src/library.cpp`): the systems in `/run/banana/systems.conf` (`/etc/banana/systems.conf` with the card's `/data/banana/systems.conf` merged over it per system by `libexec/banana/systems-merge` at the menu's start),
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

**First-time setup** (0.5.0): when `banana/setup-done` is missing or isn't `setup 2` (a fresh card, or an update from
0.4.x whose file says `done`), the boot animation goes on into the arrival (ui/src/arrival.cpp: the camera zooms into
the cartridge, bananas burst out, one is followed until both panels are white, then "Welcome to BANANAOS"; A or START
skips it) and the setup fades in: a slide per step (Language when the card has more than English, Wi-Fi with a real
test (`wifi test`: an address, the internet, the clock), Time zone, DS name, RetroAchievements (the login checked with
the site), Box art, a summary). Each slide starts from what the card has (a network from before is tested at once, an
account's login checked at once, art already fetched counted), so A straight through keeps everything; Skip (or
START) leaves a step. `banana/setup-step` keeps the slide reached: a restart (a language picked, a power cut) comes
back to it without the arrival. The accounts go to `banana/accounts.conf` (`ra_user`, `ra_password`); run-drastic
hands the RetroAchievements one to SuperDrastic (`DSFLIP_RA_*`) and run-retroarch writes it into RetroArch's
`cheevos_*`. The end writes `setup 2` into setup-done, shows "Welcome" and opens the menu through the iris; Settings >
System > First-time setup runs the same slides (no arrival) and ends back in Settings.

**Wi-Fi** (Settings > Network): the networks around from `wifi scan` (wpa_cli's scan results, one row per name,
strongest first, with its signal, a lock, *Saved* or *Connected*), run on a thread so the menu never waits. A on a
network with a password opens a keyboard: four rows of ten keys in three layers and a row of wide keys, driven by
the d-pad (A types, B deletes, X capitals, Y space, START joins) or by touch on the bottom panel. Joining writes the
network's block first in `banana/wifi.conf` (WPA3-only networks get `key_mgmt=SAE`; a block for the same name is
replaced), restarts wpa_supplicant and follows `wifi status` until it has an address. udhcpc is started by
wpa_cli's action script only once wpa_supplicant is connected, so a handheld with no network in reach isn't kept
awake asking for an address. Company (802.1X) and WEP networks are listed but go in `wifi.conf` by hand. The Wi-Fi
row switches it on and off (`wifi` in settings.conf): kept across boots (rcS starts it only when it is on and
`wifi.conf` has a network) and sleeps (the hook brings it back only when it was running).

**Bluetooth** (Settings > Network > Bluetooth, `bluetooth` in settings.conf, off by default). Both handhelds' RTL8821CS
has its Bluetooth on `uart1`, a serdev device in the device trees (`realtek,rtl8821cs-bt`, enable on gpio0 PD5):
`hci_uart` (H5, Realtek's setup through `btrtl`, firmware `rtl_bt/rtl8821cs_fw.bin` and the mandatory
`rtl8821cs_config.bin`, linux-firmware's link to `rtl8761b_config.bin`). Everything is a module and
`usr/libexec/banana/bluetooth up` loads it only when Bluetooth is on, then starts D-Bus's system bus, `bluetoothd`
(BlueZ, its input plugins for controllers) and `bluealsa` (bluez-alsa, the A2DP source); `down` unloads `hci_uart`,
which cuts the chip's Bluetooth power. Bluetooth devices lists what is paired and what a search finds; A pairs (Just
Works: bluetoothctl with no agent, so devices that want a PIN or a passkey, keyboards and Wii remotes, don't pair
yet), connects or disconnects, Y forgets. BlueZ's `/var/lib/bluetooth` is `/run/banana/bluetooth`, kept on the card
as `banana/bluetooth.tar` (exFAT can't hold its `AA:BB:...` folder names). Headphones: bluealsa-cli's events write the
A2DP sink playing to `/run/banana/bt/audio`, and bananad plays there (below, Sound). Controllers call the handheld back
themselves; the headphones played last (`banana/bluetooth-audio`) are called back at boot and after a wake, the
sound held quiet meanwhile (`/run/banana/bt/hold`: bananad plays to the codec with no speaker on it, so the games keep
their pace).

The link: hci_uart runs H5 at 1.5 Mbaud with RTS/CTS flow control and DMA on `uart1`, what the chip's config asks for
(`rtl8821cs_config.bin`: 0x04928002 at offset 0x0c, flow control bit set); about 1.09 Mbit/s of payload after the
UART's 11 bits a byte, before H5's framing. Enough for every codec here but LDAC at 990 kbps, which is why LDAC runs at
660 with its adaptive bit rate. A faster UART (3 Mbaud is in btrtl's table) would need a config of our own and
testing on the hardware. Wi-Fi and Bluetooth share the chip's radio: rtw88's coexistence code and the Bluetooth
firmware split the air time by themselves (nothing to configure); a Wi-Fi scan or a large transfer while headphones
play costs Bluetooth air time.

**Bluetooth audio** (Settings > Network, `bt_audio`): bluez-alsa offers its codecs in its order (LDAC, aptX HD, aptX,
FastStream, SBC) and the headphones take the first they share. Low latency (the default) offers aptX, FastStream and
SBC (bitpool 53); Best quality LDAC (660 kbps, adaptive), aptX HD, aptX and SBC XQ. What the handheld adds to the delay
is the player's ALSA buffer (30 ms for banana-ui, SuperDrastic and BananaDS, 64 for RetroArch), bluez-alsa's 10 ms
pipe and the codec's frames (a few ms for SBC and aptX, more for LDAC); the headphones' own buffer, typically 100-200
ms, is the rest, and no codec here shortens it (aptX Low Latency and LE Audio aren't available: no encoder, and the
chip is Bluetooth 4.2). bluealsa's I/O threads run at real-time priority 25, above the emulators' audio threads; its
pipe is paced at the stream's rate, so SuperDrastic's and BananaDS's blocking writes pace them as a sound card does.
No AAC: fdk-aac's licence grants no patent rights and doesn't combine with libopenaptx's GPL-3.0. Its
settings lines are new since the 0.3.0 translation document: English in 简体中文 until the translator's next round.

**File sharing** (Settings > Network): the card's data partition as `\\<address>\banana` over SMB3, served by
the kernel (`ksmbd`, a module loaded only when sharing is switched on) and ksmbd-tools' `ksmbd.mountd`, as a guest
share forced to root (exFAT has no owners) with DOS attributes not stored (exFAT has no extended attributes). No
password, so it is off by default and the settings screen says so. banana-ui watches the ROM folders (inotify):
games copied in are read into the library once the copying has been quiet for 2 s, while the menu shows its home
screen.

**Files** (the shelf's tile, `run = banana:files` in systems.conf: one of the menu's own pages, not a program; A or
X on the tile opens it, and so does a launch of its entry, by touch or from the Carts and layout themes; a user's
`banana/systems.conf` without `[files]` gets it from the library all the same): the cards' folders and files, the
settings' pages drawn by `src/settings_files.cpp`, the work done by `src/fileops.cpp`; and the file servers', downloaded
and unpacked (below, Files' servers). Its list, always shown first (B at a card's root comes back to it): the job in
the background, if there is one, the cards, the servers; START there opens Files' settings, Add a server and Download
options (B or START goes back). This card is the ROM folder's
parent (`/data`), the second SD card `/run/sd2` while it is in (the list of cards follows the mount table, poll's
POLLPRI on `/proc/self/mounts`, while Files is open); the system's squashfs isn't shown, nor are hidden (`.`) files.
In a folder A opens (on a file, or in an empty folder, it offers the actions as X does), Y marks, X offers Paste
here, Copy, Move, Rename, Delete (A twice), Unpack... (the archives among what is marked), New folder and Mark all.
In each of Files' lists (and its servers') LEFT and RIGHT turn a page, L and R go to the first and last rows; none of
them goes in or up (A and B do): held, they repeat, so they never choose an action, here or anywhere in the settings
(an action such as Shut down, Update's apply, joining a network, Box art's downloads are A's alone). A name
typed on the keyboard is checked for what exFAT, FAT32 and Windows refuse (`\ / : * ? " < > |`, a trailing dot or
space, Windows' device names `CON`, `NUL`, `COM1`..., and the card's limit: 255 UTF-16 characters on exFAT and FAT32
alike); SELECT leaves the keyboard with nothing changed. The card's own folders (`banana`, `bios`, `roms`, `saves`,
`states` and each system's folder in `roms`, which `data-layout` makes again at every boot and the library looks in)
can be copied, not moved, renamed or deleted; marked with others, Move and Delete take the others. A paste whose
names are taken asks once: Keep both (`name (2).ext`, a two-part extension such as PICO-8's `.p8.png` kept whole,
the name cut to the card's limit), Replace (A twice; a folder merged), Skip those; a source is never replaced by
itself, and a folder can't go into itself. Copies, moves and deletes run on a thread of their own, niced, one at a
time; the page (shown once a job has run 300 ms) shows the file in hand, the bytes and the speed, and Y (twice,
within 3 s) stops between two 1 MiB blocks, or the counting before them (Files stays open till the job is done); the
outcome counts what was asked for ("Copied 1 folder"), or what of it was done. Before the first byte the room is
checked (each file rounded up to the card's cluster) and FAT32's 4 GB a file; a copy is written as
`.banana-copy-<name>` (cut to fit a name) beside its place, sent to the card 8 MiB at a time as it goes
(`sync_file_range`: the bar shows what is on the card rather than in the RAM, and Stop waits a window at most),
given its source's time, flushed and renamed into place, so a cut never leaves half a file under the real name (its
leftover goes the next time the folder is listed; a change of case cut off between its two renames,
`.banana-rename-<name>`, gets its name back there too). A folder that can't be read (a card error) fails with its
reason: it isn't copied empty, nor left behind by a move without a word. A move on one card is a rename; between the
two cards each file is copied so, and its source deleted once its folder is flushed too (the new name on the card: a
leftover is never the only copy). The cards pasted on and taken from are flushed (`syncfs`, on the job's thread)
before the outcome is told, and a folder made or an entry renamed has its folder flushed at once (`fsync`: the
second card may be taken out right after; the card's `syncfs` would hold the page up for all else on its way there,
a big file coming over SMB, say). The second card taken out mid-way (the `sd2` script's `umount -l`: `/run/sd2` is
then the mount point's own folder, in the RAM): what the job makes is checked to be on the card's file system
(`st_dev`, as the cards were when the paste was asked for), and the job ends there, "the card was taken out", no
source deleted; a paste on a card taken out before it starts, or a name typed while the keyboard was up, does
nothing. While a job runs, `/run/banana/files.pid` (banana-ui's pid) holds bananad's idle sleep; the lid still
sleeps, and the job goes on after the wake. Games moved or copied show up in the library through the ROM folders'
watch once Files is closed (a `roms` folder made or pasted on a second card that had none is offered to the library
then: `sd2 scan`, under bananad's lock); a game's play time and Favorites star go by its place, so moved or renamed
it starts without them (the page says so). The settings' pages keep their textures within a budget (8 MB at 1x,
times the panel's area; the least recently used go after each frame), as the menu's engine does, and a folder's rows
get their words only for the window on screen: a folder of any number of names doesn't fill the memory.

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

## Saves and a home server: banana-sync

Pairing (Settings > Network > Home server > Server) starts by asking which kind of server, as the sync has one at a
time. Romhost is looked for on the LAN (a UDP broadcast only it answers), its address typed when none answers, then
paired with the code Homeport shows; a Romhost on https with a certificate of its own making is paired with the page's
Check certificate row (`homesync_tls`) off, the row there unpaired and on Romhost's page, and `banana-sync probe
<address>` answers `none tls` when such a server is what stopped it. RomM is a form, as Grout's: protocol (HTTPS by
default), address, an optional port and Check certificate (for RomM only on the form: with a server paired, a change to
it holds at once, and Server opens the form on the paired address, B there choosing another server); a full URL typed in
fills the rows it names, and its path is kept for a RomM served under a subpath. Connect runs `banana-sync probe --romm
<url>`, which asks that URL's `/api/heartbeat` once, with no port guessed and no redirect followed, and answers `romm
<url> RomM <version>` (with `old` before 5.4) or `fail <why>`: `dns`, `refused`, `timeout`, `tls`, `clock` (a
certificate failure before Wi-Fi's first time sync, so the clock is the RTC's), `ca`, `redirect <location>`, `access
<location>` (Cloudflare Access's login), `denied <status>`, `http <status>`, `notromm`, `noanswer` (the list is the
usage comment's at the top of `ui/src/sync.cpp`; `denied` is a 401 or a 403, and `noanswer` carries curl's message). The
menu says each in words of its own, and offers to take the address a `redirect` went to. After `tls`, a certificate
nothing vouches for, Y shows the certificate the server gave (`banana-sync cert <url>`, one request that checks
nothing and sends nothing but the heartbeat's question: whom it names, who signed it, its dates and its SHA-1
fingerprint, as `openssl x509 -fingerprint` prints it) and A there trusts it (`banana-sync trust`): it is kept as
`banana/homesync-server.pem`, its first line naming the server, and from then on a request to that server, and to no
other, is checked against that one certificate and nothing else, no public CA among them, so another certificate shown
at that address fails as untrusted whoever signed it. The name in it isn't compared with the address, which a
certificate made for a LAN seldom carries. The check itself stays on; Forget removes the file. A certificate
trusted for a server other than the one the card is paired with waits in `/run/banana/server-trusted.pem`, checking
that server alone, and becomes the card's file when the pairing with it is written, so a pairing given up there
leaves the paired server's certificate, and its syncs, as they were. Turning the check off
(`homesync_tls = off`) is the form's Check certificate row or the settings file, a choice made there and not on the
error, since it then takes any certificate from any server; the Server row's help says when the paired server's
certificate is unchecked or trusted by hand. Then `banana-sync pair --romm <url>` runs RomM's device flow: its status line `pair <code> <url> <seconds>` is the
pairing screen, the code large on the top panel with the approval page written out and the time left, and that page as a
QR code on the bottom one (Nayuki's qrcodegen, `ui/src/qr`), for a phone where the owner is signed in to RomM, through
Cloudflare Access's login when there is one; X takes a client token's pairing code instead, B stops the pairing (its pid
is in `sync.pid`, as a sync's). Only the approval page is ever opened in a browser: the handheld talks to the API alone,
so a server behind Cloudflare Access needs a bypass for `/api`. Romhost's typed address still goes through `banana-sync
probe <address>`, which tries a bare name on http at 8092, 8080 and 80, then on https, and follows no redirect either
(one to https on the same host is added to its tries). No pairing POST follows a redirect: Cloudflare answers 8080 with
one to https, and a redirect followed turns the device flow's POST into a GET (405).

A pairing turns Auto sync on (Leaving a game) when it was off, and the Paired screen says so: saves going to the server
without a press is what pairing is for, and with Auto sync off only Sync now syncs. A pairing with RomM 5.4 or newer
(the version `romsync.json` keeps) turns Sync before playing on as well, so another handheld's newer save comes before
the game rather than as a conflict after it; a RomM before 5.4 has no sync before a game to turn on.

The handheld pairs under its model's name (RG DS, RG DS Plus), or the name it had when it pairs again; RomM's Device
name row renames it with `banana-sync rename <name>`, which updates the RomM device at once and only then keeps the name
in `romsync.json` (a sync's write-back never puts back a name it read before). Once paired, the page's rows are those of
that kind of server (RomM's states, its sync before a game and its Save slot, Romhost's games and its certificate check;
none hedged for the other), made again when a pairing or a Forget changes it.

On RomM, games and saves are apart, as in Grout. Games come by Get games, a store's download: the server's games a
system at a time (`banana-sync games`), A putting one on the download list (`banana/homesync-get.txt`, written under
`/run/banana/get.lock` by the list code the menu and `banana-sync get [--drop] <key>` share) and starting a sync of its
own, `banana-sync sync --games-only`, which brings the games and leaves the saves alone, since a RomM library can be
hundreds of GB and a download is no reason to move saves. A on a game whose download failed tries it again at once. Any
sync of the games brings what the list holds, games asked for while it runs included (before its saves when it can, so a
game's save comes with it; those asked for during the saves after them, with `save.lock` let go, so the sync after a
game never waits on a download; one asked for as a sync is ending is brought by another the menu starts, once), and
takes each off once its files are in place, or a file of the owner's own is at its path; a download that failed stays
for the next sync, its row saying Failed and why (the sync writes `banana/homesync-get-failed.txt`), and Auto sync on
Wi-Fi carries the list on. Y searches the titles and file names, every system's from the systems list, one system's
inside it.

The server's list is kept on the card (`banana/homesync-server-games.tsv`, its first line the server's address and when
it was listed), as the full list took over half a minute on a real server of 742 games: Get games opens from it at once,
saying how old it is, and X (Refresh) asks the server again with the kept list still up. `banana-sync games` writes it
whole, and every sync that lists the server's games (the saves do, unless it is one game's sync) replaces the systems it
listed, keeping a skipped system's; Forget removes it, with the download list and its failures, as does a pairing with
another server (the same one paired again keeps them), and a list naming another server is not shown.

Once on the card a game is the owner's like any copied over File sharing: nothing updates it, nothing removes it when it
leaves the server, and its gamelist entry is written once and not kept to be taken out (a game asked for that was on the
card already, copied by hand, is adopted with the entry its owner gave it).

Saves follow what is on the card, however it came: every game the server has (found by platform and file name, and
the server's by its contents: a file of that name with other bytes, another dump or revision, is said in the sync's log
and not synced) syncs its saves, and a server's save for a game that isn't on the card stays there. Systems leaves a system's saves out; one
game's can stay on this handheld by its page in the library (X on the game, Save sync), a line `<system folder>/<ROM
file name>` in `banana/homesync-local.txt`: neither sent nor taken, and the sync before or after that game is done at
once.

On RomM 5.4 and newer each game's save is its ROM's `autosave` slot, the one Grout and RomM's own page use, unless Save
slot (a row on RomM's page, there for 5.4 and newer) names another: `homesync_slot = custom` and `homesync_slot_name`, 1
to 64 ASCII letters, digits, spaces, `-` and `_` (no colon, so `<rom id>/<slot>` in `romsync.json` can't be taken for a
state's `<rom id>/state:<name>`; a name that isn't one is said in the log and autosave used). There is no slotless
choice: RomM keeps a slotless save as one row per file name and emulator, written over in place, with no versions, no
upload guard and nothing in its negotiation, so two devices' changes couldn't be told apart. A slot changed is followed
by the next sync: a save waiting to be sent goes to the new slot, what was held or in conflict with the old one is let
go (said, its copy kept in the vault, and the Saves page stops listing it as soon as the setting changes), and the
versions agreed stay per slot, so going back finds them agreed; in the new slot every game is a save never agreed with
it.

Romhost is unchanged: the card follows the server's list (Games to sync: All, or the games ticked in Choose games), and
Homeport decides the rest.

Get games and the gamelist name a RomM game by its title and the tags RomM parses from its file name (a revision, a
hack's `[Hack by ... v1.0]`, a translation), so a hack doesn't read as a second copy of its base game; the regions
follow only where two games of a system would share a title, and the file names where that still doesn't tell them
apart. A compressed ROM's hashes on RomM (zip, 7z, rar, tar and its compressed forms, gz, bz2; the list is
`ui/src/sync_romm.cpp`'s) are of what is inside, so a download of one is checked by its size, and a zip also by its
members' CRCs against RomM's `archive_members`; any other file by its hash.

The sync never replaces a save without keeping it: `banana/save-vault` holds a copy of every save it replaced, and on
RomM what it won't decide alone it holds. A save changed on both sides stays as it is on each until its owner chooses,
before the game starts or on Settings > Network > Home server > Manage saves, whose row (and the Home server row a level
up) counts the conflicts and the saves held back. That page also lists the kept copies, which it can put back; nothing
on the handheld removes one. All of it goes through `banana-sync` (`saves`, `resolve`, `restore`), under
`/run/banana/save.lock` and never under a running game. Choosing the server's works with no network, from the vault.
Keeping the card's is an ordinary sync that knows the choice, so a server that moved meanwhile is held again.

What the sync holds to, whatever happens around it:

- A save gone or blank on the card since it last synced is never put back by itself: the server's copy goes to the
  vault, the menu asks before the game starts (take the server's, or play without), and the Saves page offers it. A new
  save written there afterwards (the game started over) is not the old save played on, so it is decided as one that
  never agreed, at every sync until a version is agreed: the same bytes as the server's agree, anything else is a
  conflict, both kept, never a version sent over the server's as the newer. On a RomM before 5.4 the card's saves never
  go up, so only the first half applies: the save is noted on the Saves page, its last good copy in the vault, with
  nothing fetched to take back. On Romhost, Homeport decides, by its plan.
- A save put in place has the quick-resume states that belonged with the old one removed: BananaDS's resume state, and
  RetroArch's automatic slot (Quick resume) in each core's states folder, or `states/` with sorted states off. (A save
  in the flat `saves/` doesn't say its core, so every core's folder but the DS's is looked in by the save's name:
  another system's game of the same file name, which shares that flat save too, loses its resume state with it.) Loaded
  over the new save, one would put the old save's memory back, and the emulator would write that over the new save at
  its next save, which the sync would then send as the newer. They are removed before the save changes, so a write that
  fails costs a resume state at most. A DS game's resume state is also loaded only over the save the game was quit with:
  as a DS game ends, the menu tells `nds-saves` what the two saves hold (`--played`), and so does bananad when it quits
  the game itself (Power off after sleeping, a shutdown under the game); at the game's next start `nds-saves` drops the
  state of the emulator starting when its save is another since (one copied onto the card by hand, or gone) rather than
  load it. With no record (a card from before this) the state is loaded. File times decide none of this: the handheld's
  clock is lost with its battery, and a state judged older or newer than its save by their dates would be judged wrong
  with it.
- A sync of the saves placing one checks that no game is running before each write, and the menu marks a game as
  starting (`/run/banana/game.starting`) before it waits for the save lock, so a sync that keeps the lock past the 2 s
  the menu gives it leaves that game's save unplaced for the next sync to decide, rather than placing it under an
  emulator that is reading it. Before the game started again after Power off after sleeping, a sync of other games
  leaves that one as it is, on any server (`/run/banana/resume-once`, `banana/resume-boot`): its save and its states,
  the resume state among them, stay as they are on the card. The menu starts no sync after a game whose end is that
  shutdown.
- The sync after a game with the server out of reach keeps a copy of the game's save in the vault at once, on any
  server, and so does an upload that fails. On RomM the save is also left waiting to be sent, which the sync when Wi-Fi
  comes back sends: under its slot when the card has synced the game before, else under the game's ROM until that sync
  finds its slot, where it is decided as any save never agreed (the server's own bytes agree, others are a conflict,
  both kept). A DS pair that can't be brought together has both of its saves kept and is not left waiting: which of the
  two is the save is settled at the game's next start. A save that waited and was never sent, the server's then taken
  over it, stays in the vault, said in the sync's log, as nothing else names it.
- A sync of every save or of games on RomM asks the server's heartbeat first, and one with no answer at all ends there
  (`error noconnect <why>`, the Sync now row saying No connection) with nothing touched. Sync now (and Get games' A)
  pressed while Wi-Fi is coming back, as it is for a few seconds after a wake, waits for it as the sync before a game
  does, up to 20 s, rather than starting a sync that can't find the server's name. The image's libcurl has no messages
  of its own, so the usual failures (a name not found, refused, no answer in time, a certificate) are put in words by
  the sync.
- DraStic's and BananaDS's saves not brought together before a sync (both changed, DraStic's unreadable, or a read or a
  write that failed) leave that game out of that sync, so a save that may be the older of the two is never decided on; a
  failure is tried again at the next sync. Both changed is settled at the game's next start: the emulator starting plays
  its own, and the other is kept in the vault (`nds-saves --prefer`).
- Every file is written beside its name, synced, and renamed over, the folder synced after; the exFAT card doesn't make
  the rename itself atomic under a power cut, so the journal's recovery puts a whole new file found beside an empty name
  in place, and the sync's state file is read from its copy beside it when the rename lost it. The record of a recovered
  install is written before the journal closes it, so a second cut recovers it again. An install stays open in the
  journal until its agreement is recorded, so a run killed between the two (the menu's last resort when a stop isn't
  heeded) is agreed at the next start, even when a game was played over the save meanwhile: the game's save is then a
  change from that version, sent, not a conflict with it. `nds-saves`, `gba-seed` (the copy a GBA game with no save in `saves/gba`
  yet gets from a core's folder, which both frontends' launchers run) and `saves-flat` sync before their renames the
  same way, under names of their own, so two writers of one save never meet in one temporary file; those names end
  `.tmp`, so a cut run's leftover is never taken for a save, and the next run of the same script for that save clears
  it.
- The pairing is never written back by a sync that outlives a Forget, and a pairing that waited for its code merges what
  the saves agreed meanwhile instead of writing a stale copy over it.

Around a game, `banana-sync sync --rom <path>` (after a game, and `--auto launch` before one) decides that game's save
alone, and its states when they are on, except before a game; after a game Romhost and a RomM before 5.4 sync every
save, and before one nothing. It takes `/run/banana/save.lock` but not `sync.lock`, so it runs beside a sync copying
games, and each run writes back only the part of `banana/romsync.json` whose lock it holds; it says how far it got in a
file the menu names for that game's end (`--report /run/banana/gameend-<ms>.status`, so an earlier game's sync still
going writes to its own, and the file removed once its run is over; `/run/banana/gameend.status` when run without it),
not on the Home server page, whose Stop is for a sync copying games. The menu reads that file and says what came of it
in the banner at the top: that the save is syncing while a run longer than a second goes on (a press hides it), then
that it was synced, was up to date already, is in conflict (settled on Manage saves) or was not synced, with
banana-sync's own words beside it: a note such as a state kept in the vault, the reason it failed, and, only when the
save was left waiting in the vault with the server out of reach, that it is kept to send with the next sync. A game
whose saves stay on the handheld says nothing, and a run whose "running" line is 120 s old, with `save.lock` free (a
long upload holds it), is let go. A game on the second SD card is on the handheld as one on the card is, its save on the
card. The menu's side is `ui/src/gamesync.cpp`: a wait in its main loop before the game, about 8 s for Wi-Fi coming up,
then 5 s for the server's first answer in `/run/banana/launch.status` (a `waiting` line there is another sync's lock on
the saves, which the 5 s don't count), 20 s in all from the wait's start, B plays now, and nothing waited for the game
started again after a power-off; then `save.lock` held for the game's length, `/run/banana/game.starting` written before
the wait for it. The wait asks about a conflict (A this handheld's, X the server's, B later), a save gone or blank here
with the server's to take back (from `/run/banana/launch.conflict`, the Saves page's lines of that game, which the sync
before it leaves), a newer server save that didn't arrive, and a choice that couldn't be carried out. A stop (B, or the
20 s) gives banana-sync 3 s to finish placing a save, then kills it and what it started, the menu drawing meanwhile.

Where each game's save is (`saves/nds`, `saves/gba`, or the flat `saves/` every other RetroArch system's core and
BananaFront write since 0.5.3) is `ui/src/save_paths.cpp`; before deciding anything the sync runs `saves-flat`, which
the launchers run too, so a save still in a core's folder from before then is the one synced. The sizes and forms a
system's saves come in, a DraStic footer cut, mGBA's clock bytes left off, are `ui/src/save_adapt.cpp`.

The tests: `ui/tests/fake-romm.py` stands in for RomM 5.4.1 (slots, devices, negotiation, and `/test/fault` and
`/test/strict` hooks). `OUT=ui/tests/out ui/tests/sync-slots.sh` checks it against `ui/tests/romm-conformance.py`, cases
measured on a real 5.4.1, run against a real server too when `ROMM_URL` and `ROMM_TOKENS` (three client tokens of one
user, as JSON) are set; then `ui/tests/romm-peers.py` (a Grout-style client and one writing as this sync does), the card
helpers in `ui/tests/sync-card.sh`, and the sync's saves by slot on several cards sharing one stand-in, every card's
token strict, with the real `nds-saves` on its card. A change to the stand-in's RomM behaviour needs a conformance case
that passes on a real RomM first; its test hooks (faults, clock, seeds) don't. `OUT=ui/tests/out
ui/tests/romm-interop.sh` plays the interoperability table through (Grout's stand-in and a BananaOS-label peer, the
server's rows recorded around every run: none may go or change), on the stand-in or a real RomM; with `GROUT_SRC` (a
Grout checkout at the commit in `ui/tests/GROUT`) Grout's side is Grout's own sync code, built headless by
`tools/grout-peer.sh` from `ui/tests/grout-peer/main.go` (Go and the SDL2 development packages). Both read the server
through `ui/tests/romm-api.py`, which fails on a wrong answer rather than printing nothing. `ui/tests/sync.sh` keeps the
ROM sync, pairing, TLS, a RomM before 5.4 and Romhost on its own simpler card, and drives the menu's wait before a game
(its `wifi`, `launchpath` and `gameend` script verbs, `ui/tests/fake-sync.sh` as the sync) and the Saves page;
`ui/tests/gamesync-test.cpp` has the wait's helpers and `ui/tests/vault-test.cpp` the vault and the save forms;
`ui/tests/paths-test.sh` has the save paths. `ui/tests/run.sh` runs all of these; CI runs
`system/tests/launchers-test.sh`, which has `nds-saves` on a made-up card. Run by hand, `tools/qemu-sync.sh` (with
`tools/qemu-lib.sh`, which `qemu-boot.sh` shares) runs an image's own `banana-sync` and `nds-saves` in QEMU against the
stand-in or a real RomM: pairing, each way, power cuts, a conflict on the prompt and on the Saves page, the server gone.

## Files' servers: banana-fetch

Files' list has the file servers beside the cards: a NAS's or a computer's shares over SMB, SFTP or FTP, read only
(nothing is written, renamed or deleted on a server, nor copied from one server to another). Their pages are the
settings' too (`src/settings_servers.cpp`): a server's folders are browsed with Files' keys (A opens a folder, Y marks,
X: Download, Copy, Mark all; B goes up, out of the start folder back to Files' list, or there when the folder above
doesn't come: the server unreachable, or B again while it is awaited; LEFT and RIGHT turn a page, a choice's value on
its row). Two ways to where a download goes:

- **Download** opens its page with the folder guessed: where this server folder's downloads went last time (kept per
  server folder in `nas.conf`, as `card:folder`, any folder on either card), else the system the files belong to by
  their extension (an archive's suffix looked through; zip and an extension two systems share say nothing), else by
  the server's folder names from the deepest up (a system's id, folder, name, systems.conf's `aliases` and the usual
  names: `src/sysfolders.cpp`), else `roms`. A on its Folder row opens Files' own navigation on that folder (or the
  nearest one there) to choose another: the cards, their folders, Y takes the folder shown (A too, on a file or in an
  empty folder; on a folder A opens it), X makes a new one (opened at once); B from the cards gives the choice up, the
  page as it was. The Card row takes the same folder on the other
  card (made when the download starts).
- **Copy** puts the marked files in Files' clipboard (the server, its folder, the names): in any folder of a card,
  X, Paste here opens the Download page on that folder. A server's files and the cards' share one clipboard.

The page then asks what Download options leave to it (`files_*` in settings.conf: ask, always, never): whether
archives are unpacked and deleted after, and, when some of the files are on the card already, Skip them, Replace them
or Keep both (`name (2).ext`, as Files' paste). A server's folder named after the system the folder chosen is (its
"GBA" into `roms/gba`) goes into it itself; into any other folder it goes as a folder, as a paste does. Banana's own
folder, `banana/`, is never a download's or an unpacking's (its settings, its servers, its jobs). Archives on a card:
X, Unpack... on one or more (the first volume of a multi-volume one) opens the same page, the folder they are in
first; one is looked into first (its password typed, a dictionary bigger than the memory free said), several go as
they are (one with a password is left packed, and its row on the job's page shows it in Files to be unpacked from
there). The job's page shows the job in hand: B leaves it working, X stops it (its parts and the job kept: A goes on
from there), Y cancels it (the job deleted with what its unfinished downloads left; the files it finished stay).
Leaving it (B, the job cleared or cancelled) goes back where it was opened: the server's folder, the card's folder, or
Files' list from the job's row there. While the job works, its `.part` files show in their folder as downloading, and
Files leaves them alone.

banana-ui has no network code for it: `banana-fetch` (`src/fetch/`) does it, in two shapes.

- `banana-fetch serve`, while the pages need it (started at a server's first page, an archive looked into, a server
  saved): a line protocol on its stdin and stdout (tab-separated fields), one request at a time, its answer read on a
  thread (which wakes the menu's loop when it is in): log in, list a folder, walk the folders chosen into the files to
  download and where each goes, look into an archive, add or change a server. A listing or a walk that finds the
  connection gone (an idle NAS hung up, Wi-Fi slept) connects again once; when that fails on the network too (Wi-Fi is
  back only a while after a sleep: unloaded before it, loaded again after), the next request tries again. A paste of
  another server's files than the one logged in to logs in to that one first. It goes when Settings closes, or after
  10 idle minutes.
- `banana-fetch run`, the jobs: `banana/downloads/<time>.job` on the card (the server, the files with their sizes and
  where each goes, or the archives and the folder; unpacking, the archive deleted after; the card's root and its file
  system's `st_dev`), done one after the other in the background while the menu shows anything else. What happened to
  each file is a line of `<job>.done`, so a job cut short (stopped, the battery, a crash) goes on from there when A
  resumes it, and a file half downloaded goes on from its `.part`. Its pid is `/run/banana/transfer.pid`, its progress
  a line in `/run/banana/fetch/status` (twice a second at most, in a folder of its own so that the menu's watch on
  `/run/banana` isn't woken by it), which the pages read. A job done becomes `<time>.finished` and is told in
  `/run/banana/fetch.finished`: the menu then says the finished jobs it hasn't seen in a banner over the top panel,
  whatever it shows (and offers a `roms` folder a download made on a fresh second card to the library). It runs
  niced, with idle I/O priority, and banana-ui stops it (SIGSTOP to its process group, 7zz with it) while a game runs,
  and lets it go on after the game, or when the menu starts again (one that went away during a game). It runs as
  root and the job is a file on the card, so it writes only on the cards (inside the card the job is for, never
  `banana/`), reads a password file only of `/run/banana`, and checks before each folder it makes, each try at a file
  and each unpacking that the card is still the one the job was written for: the second card taken out (the `sd2`
  script's `umount -l`, its mount point then a folder of `/run`, the RAM), the job stops there, kept, and goes on once
  the card is back.

Protocols: SMB 2 and 3 (libsmb2: a NAS's shares, Windows' and macOS's file sharing; a server's shares are its
root; a guest logs in over SMB 3.0.2, or SMB 2 on an older server, as libsmb2 6.2 signs a 3.1.1 guest's requests with
a key the server doesn't have), SFTP (libssh2: a password, or keyboard-interactive; the server's key trusted the
first time and checked after), FTP and FTPS (libcurl: passive, explicit TLS, the certificate trusted the first time
and checked in OpenSSL's own verification, before the password is sent; MLSD, or LIST where a server has no MLSD;
names in CP1251 or CP866 for old servers). A file goes into `<file>.part` next to where it belongs and is renamed once
its size is right; its name is made one the cards take, as Files' own names are (`\ / : * ? " < > |` to `_`, no
trailing dot or space, a `_` before a hidden name or a Windows device name, cut before its extension to 250 UTF-16
characters, the cards' 255 less `.part`). A drop (Wi-Fi coming back after a sleep, a server restarting, on any of the
protocols) is tried again after 3, 10, 30, 60 and 60 s from where it stopped, and a connection quiet for 30 s is
dropped and tried again. What another try won't change (the login refused, a key that changed, a file that isn't there
or isn't allowed, a full card) fails the file at once, and the job goes on with the next. The room needed and FAT32's
4 GB a file (the second card may be FAT32) are checked before anything is written.

Archives: zip natively (stored and deflated, zip64, CRCs checked; names in UTF-8, or in CP866 or CP437 as old Windows
zips have them), 7z, RAR and the rest (an encrypted zip, another method) with 7-Zip's own `7zz` (built from its
source: Buildroot's p7zip has no RAR), on one thread (several take many times the memory). A multi-volume archive
(`.7z.001`, `.partN.rar`, `.rar` with `.r00`...) is unpacked once its last volume is in. Unpacking goes into
`<card>/.banana-unpack/`, on the same card, and its files are moved into place when it worked; an archive with a
path out of it (`..`, an absolute one) is refused whole, macOS's and Windows' clutter (`__MACOSX`, `.DS_Store`,
`Thumbs.db`) and links are left out, names the cards can't take are changed (the same name twice gets ` (2)`, and a
file never takes the place of a folder of its name, nor a folder a file's), and an archive holding a single folder is
unwrapped (Download options). An archive whose dictionary wouldn't fit in the memory free is left packed, as is one
with a password: Unpack... in Files unpacks it, the password typed there. Zips for a system whose emulator plays them
zipped (the DS, the GBA...) are kept as they are unless asked, each archive judged by itself (the job's
`keep_zips`): the Download page's Unpack archives decides for a choice of zips alone, a row of their own when the
choice may hold 7z or RAR archives too (a folder), which are unpacked all the same.

Servers are in `banana/nas.conf` (sections of key = value, read as data, never run). A password is kept only when
Remember password is on, sealed: AES-256-GCM under a key made (HKDF-SHA256) from `banana/nas.key` (32 random bytes)
and the SoC's serial number (`/proc/device-tree/serial-number`), which isn't on the card, and bound to the server's
protocol, address, port and user. The card read on a computer, or put in another handheld, doesn't give it away
(Files asks for it again); someone with the handheld itself can still open it, as there is no secure store. That
rests on the serial number, which U-Boot puts in the device tree: without one, the key is made from `nas.key` alone,
so the card alone opens the passwords, and banana-fetch says so in `banana/logs/downloads.log` when it seals one.
File sharing leaves both files out. A password not kept goes to the runner in a file of `/run/banana` (tmpfs, 0600)
that it reads once and deletes, and an archive's goes to 7zz on its stdin (where 7zz asks for it), not with `-p`:
passwords are never on a command line nor in `banana/logs/downloads.log`.

`ui/tests/fetch-test.sh` checks the archives (7z and RAR test archives, and zips made by Python: CP866 names, a zip
slip, zip64..., and a 7z of 1502 files), the session and the runner (cuts, stalls, `.part`s, a full card, passwords,
a connection lost between listings, names made for the cards, a job's paths outside its card or in `banana/`, its card
taken out) against a folder standing in for a server (`BANANA_FETCH_TEST=1`); `ui/tests/run.sh` goes through the pages
headless with it (Files' list, a folder chosen and made through its navigation, a paste of a server's files, an
archive unpacked, taps on the buttons, and a runner at work beside the menu); and `ui/tests/fetch-servers.sh` runs
against real servers in Docker (vsftpd for FTP and FTPS, pure-ftpd for MLSD, OpenSSH, Samba with a guest share), a
download on each protocol cut in the middle by a proxy and resumed.

## Sleep: bananad

`bananad` reads every input device next to the menu and the games (evdev allows several readers; it grabs
nothing). Closing the lid (the hall sensor, `SW_LID`), a short press of power, or 10 idle minutes in the menu put
the handheld to sleep:

1. The game's process group gets `SIGSTOP` (ROCKNIX found DraStic's dynarec must not run on while the clocks drop).
2. `/etc/banana/sleep.d/*` run with `pre` (Wi-Fi down and its driver unloaded: the RTL8821CS doesn't survive a
   suspend otherwise; nothing at all when Wi-Fi is off. Bluetooth down the same way, its modules unloaded, the sound
   held quiet so a woken game isn't loud on the speakers until the headphones are called back).
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
  fraction of a millisecond of the kernel returning; then the `post` hooks (Wi-Fi and Bluetooth back) in the background, the LEDs,
  the headphone route; the press that woke it is ignored.

**Power off after sleeping** (Settings > Sleep & power; `poweroff_after_sleep_minutes`, 0 = never, the default; 15,
30, 60 or 120): the RTC alarm is then the sooner of the battery check and that deadline, counted from the first real
sleep. When it is reached (its own alarm, or any wake with the lid still shut after it) the handheld is woken
quietly: the panels stay dark, no `post` hooks, nothing for the menu, the volume down (the levels stored first, so
the next boot has them). The game saves a resume state and quits, by what each emulator already does:
SuperDrastic `SIGUSR1` to `drastic` (run-drastic always sets `DSFLIP_RESUME_FILE`; with Quick resume off,
`DSFLIP_RESUME_ON_QUIT=0` keeps the player's own quits unsaved), BananaDS `SIGUSR1` to `bananads` (the resume state
whatever `resume.on_quit` says), RetroArch through its command pipe (`STATE_SLOT_MINUS` down to the automatic slot,
`SAVE_STATE`, `QUIT`, which waits for the save task; run-retroarch puts the slot back), anything else `SIGTERM`.
bananad waits for the game to quit (60 s at most; 10 for `SIGTERM`), writes `banana/resume-boot` (the ROM and the
emulator) for a game that saved, and shuts down cleanly. The lid can't switch it on again: the hall sensor is the
SoC's GPIO0_C3 (`gpio-keys-hall`), unpowered once the RK817 is off, and the RK817 starts only from its own power key
(or a charger). At the next boot (power pressed) banana-ui takes `resume-boot` (deleted before the game starts, so a
game that fails isn't started at every boot), names the ROM in `/run/banana/resume-once` and launches it; the
launcher loads its resume state once, Quick resume on or off. On the charger ("dark and awake") the deadline doesn't
count: nothing drains, and the charging light would go out; unplugged, it starts at the real sleep.

A download or an unpacking of Files' (banana-fetch's runner, its pid in `/run/banana/transfer.pid`) with no game
running holds the lid's sleep and the idle one: the lid shut makes the panels dark, the job goes on, and the handheld
sleeps once it is done (`sleep.log` says so). The power button sleeps at once, as ever, and the job goes on from where
it was after. A runner that is stopped (`T` in `/proc/<pid>/stat`: banana-ui went away during a game) holds nothing.

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
don't have (other panels, touchscreens and codecs, HDMI, cameras, USB gadget mode, packet filtering, Bluetooth
adapters other than the RTL8821CS's UART, FUSE, swap, netsched), so the kernel in memory is smaller and boot probes
fewer drivers; the modules are Wi-Fi, the GPU, xpad, file sharing, Bluetooth (loaded only while it is on) and virtio
for QEMU. Wi-Fi's scans back off to one every five minutes when no
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
`Master` on the rk817. Bluetooth headphones, while connected, come first: bluez-alsa's PCM plugin through `plug`
(A2DP: aptX, SBC, LDAC... as above), and the volume keys change the headphones' own volume (`bluealsa-cli volume`, 0-127). A program opened
before a change keeps its device until it opens it again. bananad also sets
the rk817's capture gains at boot (`Mic Capture Gain` and `Master Capture Volume`), after `alsactl restore`, so a
state saved by another firmware can't leave the DS's microphone muted; the emulators open `default`'s capture side
unless Settings > Nintendo DS turns the mic off. Playback and capture of one card share its I2S DAI, and
`rockchip-i2s-tdm` marks that DAI `symmetric_rate`: the two streams must run at one rate. bananad therefore pins
both directions of `default` to S16 stereo 48 kHz (the rate the DS emulators already play at) and ALSA's `plug`
resamples the emulators' 44.1 kHz capture to it; opened at 44.1 kHz against a 48 kHz playback the capture's
`hw_params` was refused, and the microphone never started whatever the gains were.

**Speaker EQ** (Settings > Display & sound > Speaker EQ). On the speakers only, bananad's playback slave is
`{ type banana_eq slave.pcm "hw:N" }` inside the `plug`: banana_eq is an ALSA extplug (`ui/src/eq/pcm_banana_eq.c`,
installed as `/usr/lib/alsa-lib/libasound_module_pcm_banana_eq.so`) that runs RBJ biquads on the S16 48 kHz stream in
each program's own audio thread (1.6% of one A55 core for 9 filters). It follows `/run/banana/speaker-eq`, Equalizer
APO's parametric text, with one stat every 10 ms of sound: no file passes the samples through untouched, and a new
one is heard at once, in a game already playing and in the menu's beep for the press that changed it. banana-ui writes that file at start and from the page:
the profile (`tuned`: `/usr/share/banana/speaker-eq/<board>.txt`, measured on the handheld; `none`; or a `.txt` in
`banana/speaker-eq` on the card, e.g. from AutoEq) parsed and written again with the page's seven octave sliders
after it (`speaker_eq`, `speaker_eq_profile`, `speaker_eq_bands` in settings.conf). A curve that peaks above 0 dB
gets that much less preamp, so nothing clips that didn't before. Headphones and Bluetooth keep the plain route.

## Clocks: banana-perf

| Profile | CPU | GPU | For |
|---|---|---|---|
| `ui` | schedutil, at most ~1.1 GHz | powersave | the menu |
| `powersave` | schedutil, at most ~1.4 GHz | on demand | 8-bit and 16-bit systems |
| `balanced` | schedutil, full range | on demand | GBA, apps |
| `steady` | schedutil, full range above a 1.1 GHz floor (between page flips plain schedutil dropped to 408-600 MHz and the next frame started late) | on demand | PlayStation |
| `performance` | performance | performance | SNES, N64, ports |
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

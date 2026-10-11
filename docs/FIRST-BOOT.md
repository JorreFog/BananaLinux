# The first boot on an RG DS

Banana hasn't booted on the hardware before yours. This page is what to flash, what should happen, what to try,
and which files tell what went wrong.

## Flash

| Handheld | Image (from the CI run's Artifacts, or `make rgds` / `make rgds-plus`) |
|---|---|
| RG DS | `banana-rgds.img.gz` |
| RG DS Plus | `banana-rgds-plus.img.gz` |

Write it to a microSD card with balenaEtcher (it takes the `.gz` as it is), put the card in the slot, power on.

## What should happen

1. A second or two after power-on, the boot animation on both panels: the bananas drop onto the top screen, the
   cartridge goes into the console on the bottom one. The very first boot spends a few more seconds before it,
   making the data partition from the rest of the card.
2. The menu, on the Nintendo DS with no games: "Copy .nds files to roms/nds".
3. Power off (Start > System > Shut down, or hold power 2 s), put the card in a computer: a `BANANA` drive with
   `roms/`, `bios/`, `banana/` and so on. Games go in `roms/nds`, `roms/gba`... (`banana/README.txt`).

## Things to try

- **Buttons**: A, B, X, Y, the d-pad, L, R, Start in the menu; A should be the right-hand button (the DS's).
- **Touch**: tap a cartridge on the bottom screen; the keyboard in Settings > Network.
- **Sound**: the menu's clicks; the volume keys; headphones (on the RG DS Plus the speakers should go quiet).
- **Sleep**: close the lid in a game, wait, open it. The game should carry on where it was within a moment.
  Close it in the menu; leave it closed overnight and see what the battery says in the morning.
- **Wi-Fi**: Settings > Network, choose your network, type the password.
- **A DS game**, and a game for another system (GBA, SNES...).

## Updating later

A build's `banana-<handheld>-<version>.update.tar` goes in `banana/updates/` on the card (from a computer, or over
Wi-Fi with Settings > File sharing on); Settings > System > Update lists it, A twice writes it to the card's other
system slot and checks it, A restarts into it. The version before stays on the card: Settings > System >
Previous version restarts into it. Nothing on the data partition (games, saves, settings) is touched by either.

## When something is wrong

Everything Banana knows about a boot is in `banana/logs/` on the card (put it in a computer):

| File | What |
|---|---|
| `boot.txt` | the milestones, in seconds since the kernel started: init, rcS, the data partition, the panels, the menu |
| `dmesg.txt` | the kernel's log (`dmesg.1.txt`: the boot before) |
| `banana-ui.log` | the menu's log (`.1`: the run before) |
| `sleep.log` | one line per sleep: how long it took to fall asleep, how long it slept, why it woke, how long the way back took |
| `sleep-devices.log` | with `sleep_debug = yes` in `banana/bananad.conf`: every device's suspend and resume time, slowest first |
| `superdrastic/` | SuperDrastic's logs |

`banana/panel-tune.txt` says what the panel tuner did to the refresh rate on the first boot.

Send those files back and say what you saw. Some cases:

- **Black screens, and no `BANANA` drive on the computer afterwards**: the kernel or init never got far enough to
  make the data partition. Only a serial console tells more (the RG DS's UART at 1500000 baud, `ttyS2`).
- **Black screens, but the `BANANA` drive is there**: `banana/logs/banana-ui.log` and `dmesg.txt` say why the menu
  couldn't open the panels.
- **The menu, but "Can't read the card's games"**: the `BANANA` partition couldn't be mounted (formatted as
  something other than exFAT?).
- **Slow wake from sleep**: set `sleep_debug = yes` in `banana/bananad.conf`, sleep and wake a few times, and send
  `sleep.log` and `sleep-devices.log`: they show which part of the handheld takes the time.

A serial cable on the RG DS's UART (`ttyS2`, 1500000 baud) shows everything as it happens; with `banana.console=1`
added to `extlinux/extlinux.conf`'s `APPEND` on the boot partition, rcS's milestones and the menu's log go there
too, as they do in QEMU (`tools/qemu-boot.sh`).

With Wi-Fi working, SSH saves taking the card out: put your public key in `banana/ssh/authorized_keys`, switch on
Settings > System > SSH, then `ssh root@<address>` (the address is in Settings > Wi-Fi's description). `dmesg`,
`cat /run/banana/boot-times` and `tail -f /data/banana/logs/banana-ui.log` all work there.

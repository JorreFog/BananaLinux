# What Banana gives an emulator, and what it expects back

This is the contract between the menu and any program that plays a game: DraStic (through SuperDrastic),
RetroArch, and BananaDS when it is ready. Keep to it and the rest of Banana (sleep, the volume keys, the logs,
the settings) works around the emulator without knowing it.

## Starting

`systems.conf` names a `run` command per system, with `%ROM%` the game's path (quoted) and `%SYSTEM%` the system's
id. The commands are the launchers in `/usr/libexec/banana/`: one per emulator (`run-drastic`, `run-bananads`,
`run-retroarch`), and `run-nds`, which picks the DS emulator chosen in Settings > Nintendo DS > Emulator
(`nds_emulator` in `banana/settings.conf`) or whichever is installed.

Before the launcher runs, banana-ui has:

- closed its display (it is no longer DRM master; both panels are free), closed the input devices and the sound
  device;
- set the system's clock profile (`banana-perf`, the `perf` in `systems.conf`: `superdrastic` is the performance
  governor, which SuperDrastic's own governor then lowers per game);
- written the game's pid to `/run/banana/game.pid`;
- exported `BANANA_ROM` and `BANANA_SYSTEM`, and, where set by the kernel command line (`banana.top=`,
  `banana.touch=`), `BANANA_TOP` and `BANANA_TOUCH`.

The launcher runs in its own session (`setsid`), with signals at their defaults and `/data` as the card.

## The hardware, as an emulator sees it

| | |
|---|---|
| Panels | KMS on `/dev/dri/card0`: two connected DSI connectors of the same mode, both 640x480 (RG DS) or 1024x768 (RG DS Plus). The top panel is `BANANA_TOP` (default `DSI-2`), the bottom the other one. Take DRM master, set the modes as they are (never change the timing: the DTB carries a measured 60 Hz), page-flip on vblank. Give it all back on exit. |
| GPU | Panfrost (Mali-G52), EGL on GBM, OpenGL ES 3.1; loaded by rcS after the menu. Software rendering is fine: SuperDrastic's rasterizer draws the DS in software and hands the GPU only the shader. |
| Touch | evdev; the bottom panel's touchscreen is the device whose sysfs path contains `BANANA_TOUCH` (default `fe5e0000.i2c`), multitouch protocol B, 640x480 or 1024x768. The top panel has its own, unused by the DS. |
| Buttons | evdev, one device: the ADC joypad (`rocknix-singleadc-joypad`). A = `BTN_EAST`, B = `BTN_SOUTH`, X = `BTN_NORTH`, Y = `BTN_WEST`, d-pad = `BTN_DPAD_*`, L/R = `BTN_TL`/`BTN_TR`, Start/Select = `BTN_START`/`BTN_SELECT`, F = `BTN_MODE`. Read it, never grab it (`EVIOCGRAB`): bananad reads the same device for the lid, the power button and the volume keys. The volume keys (`KEY_VOLUMEUP/DOWN`) and F + volume (brightness) are bananad's; ignore them. |
| Sound | ALSA, device `default` (bananad routes it to the speakers, the headphones or the RG DS Plus's amplifier through `/run/banana/asound.conf`). Open it once; a route change while playing is only taken at the next open (a known gap). 44.1 or 48 kHz, S16, stereo. The microphone: `default`'s capture side (the rk817). |
| CPU | 4x Cortex-A55; the governor and limits are set by `banana-perf` before the game starts. An emulator with its own governor (SuperDrastic) may lower `scaling_max_freq` and must put it back on exit. |
| Clock | The kernel's; the time zone is in `TZ`. |

## While it runs

- **Sleep**: closing the lid (or the power button) sends `SIGSTOP` to the game's process group, then suspends;
  on waking, `SIGCONT`, within a millisecond of the kernel returning. The emulator sees nothing but a gap in time:
  don't drop audio on a timing jump, and don't treat a vblank that took seconds as an error. The panels come
  back on by themselves (the kernel's DRM resume restores the last framebuffer), so no modeset is needed on wake.
- **Quitting**: a hotkey of the emulator's own (SuperDrastic: F + Start). On exit, banana-ui reopens the panels
  and records the play (anything over 5 s). Exit code 0 is a normal quit; anything else is logged.
- **Shutdown while playing**: a long power press or an empty battery sends `SIGTERM` to the process group and
  waits up to 5 s for saves to be written before powering off. Write saves on `SIGTERM`.

## Where things go on the card

| | |
|---|---|
| Games | `/data/roms/<system>` (`folder` in `systems.conf`); `.zip` ROMs are the emulator's to read |
| BIOS files | `/data/bios` (DS: `bios7.bin`, `bios9.bin`, `firmware.bin` if the emulator wants them) |
| Saves and states | `/data/saves/<system>`, `/data/states/<system>` (DraStic keeps its own in `/data/drastic`) |
| The emulator's own files | `/data/<emulator>` (settings it writes, per-game memory) |
| Logs | `/data/banana/logs/<emulator>/` (readable on a computer; keep them small) |
| Screenshots | `/data/screenshots` |

Everything under `/usr` is read-only: an emulator that writes next to itself (DraStic) is copied to `/data`
first by its launcher.

## Settings

Settings > Nintendo DS holds the choices a DS emulator should honour: renderer, shader (`none`, `ds-crisp`,
`ds-crisp-color`, `ds-grid`, `ds-grid-color`, `ds-fsr`, SuperDrastic's names), 3D resolution (1x / 2x). They are
written to `banana/superdrastic.conf` (`DSFLIP_RAST`, `DSFLIP_SHADER`) and `drastic.cfg` (`hires_3d`); a new
emulator reads them from its launcher's environment or gets its own file and a row in `ui/src/settings.cpp`
(one `add(...)` line per setting: its file, key, names and values).

## BananaDS

The slot is ready: `buildroot/package/bananads` (fill in the commit, the build and the install),
`/usr/libexec/banana/run-bananads` (the environment above as `BANANADS_*`, fill in the command line), and
Settings > Nintendo DS > Emulator. Until the package is in the defconfigs, `run-nds` runs DraStic whatever the
setting says. What BananaDS gets from the launcher:

```
BANANADS_TOP      the top panel's connector            BANANADS_TOUCH   the bottom touchscreen's sysfs name
BANANADS_DATA     /data/bananads                        BANANADS_LOGDIR  /data/banana/logs/bananads
BANANADS_SAVES    /data/saves/nds                       BANANADS_STATES  /data/states/nds
BANANADS_BIOS     /data/bios                            argument 1       the ROM (.nds or .zip)
```

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

Settings > Nintendo DS holds the emulator and Quick resume, then only the chosen emulator's own settings, each kept in
that emulator's files: SuperDrastic's shader and CPU clock in `banana/superdrastic.conf` (`DSFLIP_SHADER`,
`DSFLIP_CPUGOV`) and its 3D resolution in `drastic.cfg` (`hires_3d`); BananaDS's shader, 3D resolution and the rest in
`/data/bananads/bananads.conf` (`kms.shader`, `render.scale`, ...). Both draw 3D with the Gengis Engine, always (no
setting: the launchers set it). A new emulator gets its own file and its own rows in `ui/src/settings.cpp` (one
`add(...)` line per setting: its file, key, names and values, under its `emu` branch).

## BananaDS

BananaDS (melonDS's core with SuperDrastic's display path, governor and sound; `bananads/` in
github.com/JorreFog/SuperDrastic) is in both images. Settings > Nintendo DS > Emulator switches the DS shelf to it
(`nds_emulator = bananads` in `banana/settings.conf`); DraStic stays the default until BananaDS has run on the
handhelds themselves.

- **The package** (`buildroot/package/bananads`): BananaDS at a pinned commit of the SuperDrastic repository, built
  with CMake for the handhelds (melonDS's JIT, `-mcpu=cortex-a55`, link-time optimization, no SDL, ALSA, the KMS
  frontend). Its build would fetch melonDS at configure time; here melonDS is a second download at the commit
  BananaDS pins, and BananaDS's patches to it are applied by Buildroot's patch step. `/usr/bin/bananads`.
- **The display**: `frontend=kms`: DRM master, both panels' modes as they are, each frame's lines drawn straight
  into dumb buffers (no copy) and both panels in one atomic commit from a `SCHED_FIFO` presenter, the plane scaling
  256x192 (512x384 at 2x) to the panel; frames queue one refresh, so a game alternating 19 ms and 12 ms frames
  still shows a new frame each refresh. In QEMU the virtio GPU can't scale planes, so the frame is copied and scaled
  by a whole number on the CPU instead.
- **Input**: the pad and keyboards read (never grabbed), the bottom touchscreen by `BANANADS_TOUCH`. F + Start quits
  (with the resume state), F + R saves slot 1, F + L loads it.
- **In-game menu**: L3 pauses the game and opens SuperDrastic's menu, in BananaOS's colours, on both panels: Resume,
  Save and Load (8 slots with their pictures; a load can be undone), Quick settings (volume and brightness the way
  bananad keeps them, the microphone's meter and a blow), Restart game, Quit game. Touch works on it too.
- **Clocks**: its own governor: under `banana-perf superdrastic`'s `performance` governor it lowers
  `scaling_max_freq` a step at a time while the game keeps the DS's pace, steps up as soon as it falls a frame
  behind (and keeps off that clock for a while, remembered per game in `/data/bananads/cpugov`), and puts it back on
  exit.
- **Quitting**: F + Start, or `SIGTERM` (shutdown while playing): the resume state, then the save, then the panels
  are given back.

What the launcher (`run-bananads`) gives it: the contract's names, and the same as BananaDS's own settings (it reads
every `BANANADS_<KEY>` as the setting `<key>`, over `/data/bananads/bananads.conf`):

```
BANANADS_TOP      the top panel's connector            BANANADS_TOUCH       the bottom touchscreen's sysfs name
BANANADS_DATA     /data/bananads                        BANANADS_LOGDIR      /data/banana/logs/bananads
BANANADS_SAVES    /data/saves/nds                       BANANADS_STATES      /data/states/nds
BANANADS_BIOS     /data/bios                            argument 1           the ROM (.nds or .zip)
BANANADS_FRONTEND kms                                   BANANADS_LOG_FILE    logs/bananads/bananads.log (.1: the last)
BANANADS_SAVE_DIR /data/saves/nds                       BANANADS_STATE_DIR   /data/states/nds
BANANADS_RENDER_RENDERER gengis (always)                DSFLIP_SHADER_DIR    SuperDrastic's shaders
```

`/data/banana/bananads.env` (shell assignments) adds settings for every run, e.g. `BANANADS_KMS_STATS=600` (the
frame statistics in the log) or `BANANADS_POWER_CPU_LATENCY_US=100` (the cores kept out of deep idle while a game
runs: ~1.5% faster, a battery cost not measured yet). BananaDS needs no BIOS or firmware files (melonDS's FreeBIOS and a
generated firmware).

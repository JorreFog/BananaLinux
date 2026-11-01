# What Banana gives an emulator, and what it expects back

This is the contract between the menu and any program that plays a game: DraStic (through SuperDrastic),
RetroArch and BananaDS. Keep to it and the rest of Banana (sleep, the volume keys, the logs,
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
| Sound | ALSA, device `default` (bananad routes it to the speakers, the headphones or the RG DS Plus's amplifier through `/run/banana/asound.conf`). Open it once; a route change while playing is only taken at the next open (a known gap). 44.1 or 48 kHz, S16, stereo; playback and capture of one card share an I2S DAI whose driver is `symmetric_rate`, so bananad pins both to 48 kHz and ALSA's `plug` resamples (a capture opened at 44.1 kHz against a 48 kHz playback is refused — the reason a game's mic stayed silent). The microphone: `default`'s capture side, which bananad points at the rk817 (the real microphone) and whose capture gains it sets at boot. Both emulators open it unless the card's settings turn it off (`DSHOOK_MIC_THRESH = 0`, or BananaDS's `mic.source = none`). |
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
- **Power off after sleeping** (Settings > Sleep & power): asleep past the deadline, bananad continues the game with
  a request to save a resume state and quit, then shuts down: `SIGUSR1` to `drastic` (SuperDrastic) or `bananads`,
  RetroArch's `STATE_SLOT_MINUS` (to the automatic slot) + `SAVE_STATE` + `QUIT` on `/run/banana/retroarch.cmd`,
  `SIGTERM` to anything else. It waits up to 60 s. At the next boot banana-ui launches the game again with its ROM in
  `/run/banana/resume-once`: the launcher deletes that file and loads the resume state this once, even with Quick
  resume off (SuperDrastic `DSFLIP_RESUME_LOAD=1`, BananaDS `resume.on_start` left on, RetroArch
  `savestate_auto_load`).

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

RetroArch keeps a save folder per core (`/data/saves/gpSP`, `/data/saves/mGBA`...), except for the Game Boy
Advance: every GBA core uses `/data/saves/gba/<game>.srm` (run-retroarch), so a game can switch between gpSP and
mGBA (Settings > Other systems) with the same save. A game with no save there yet gets a copy of its newest
non-blank one from a core's folder; those files stay as they were. States stay per core (their formats differ).

The two DS emulators share their in-game saves: `nds-saves`, run by both launchers before a game, writes the
newer of DraStic's `drastic/backup/<game>.dsv` and BananaDS's `saves/nds/<game>.sav` over the other in its own
form. DraStic's `.dsv` is the raw save; a `.dsv` from DeSmuME (a card scraped under another firmware) carries
DeSmuME's footer, which `nds-saves` reads (`padded_size`, 36 bytes from the end) and leaves off for BananaDS.
The size is kept exactly — melonDS picks the save memory from the ROM, and a file of another size is what makes a
game report its save data as corrupted — and the file replaced is kept as `.bak`. A newer file under 512 bytes is
no save (the smallest, a 4 Kbit EEPROM's, is 512): it never goes over one with something in it, which goes over it
instead (#96). The emulators' own save states (`.dss`, `.mst`) are not shared.

## Settings

Settings > Nintendo DS holds the emulator and Quick resume, then only the chosen emulator's own settings, each kept in
that emulator's files: SuperDrastic's shader and CPU clock in `banana/superdrastic.conf` (`DSFLIP_SHADER`,
`DSFLIP_CPUGOV`) and its 3D resolution in `drastic.cfg` (`hires_3d`); BananaDS's shader, 3D resolution and the rest in
`/data/bananads/bananads.conf` (`kms.shader`, `render.scale`, ...). Both draw 3D with the Gengis Engine, always (no
setting: the launchers set it). A new emulator gets its own file and its own rows in `ui/src/settings.cpp` (one
`add(...)` line per setting: its file, key, names and values, under its `emu` branch).

The microphone has a row each. SuperDrastic's one key is both: `DSHOOK_MIC_THRESH` is its sensitivity and `0` turns
the mic off. BananaDS separates the two, because `mic.threshold = 0` means "gate off — the game hears everything"
rather than "off": its **Microphone** row writes `mic.source = device|none`, and **Mic sensitivity** writes
`mic.threshold`. The launchers write the mic keys a card is missing once and never touch a card that already has
them, so a choice made in Settings survives updates.

**Buttons** (Settings > Nintendo DS > Buttons) are the one setting both emulators share, so a DS game plays the same
in either: what L2, R2, L3 and R3 do, in `banana/settings.conf` (`nds_btn_l2` ... `nds_btn_r3`: L2 swaps the screens,
R2 fast-forwards on and off, L3 blows on the mic, R3 nothing by default; the RG DS Plus has no R3). F + R saves and
F + L loads a state in both, whatever they are. `nds-buttons` gives them to each emulator as it starts: BananaDS as
`BANANADS_INPUT_<BUTTON>`; DraStic as its controls in `drastic.cfg` (save and load state, fast-forward, the fake
microphone, the menu, a DS button: controls_a, then controls_b), the screens' swap as SuperDrastic's
`DSFLIP_SWAP_BUTTONS`. DraStic's save and load state controls always have a button, joystick buttons 30 and 31 (no pad
has them) when none of the four is set to them: SuperDrastic presses them for F + R and F + L, its menu's Save and
Load and Quick resume (`buildroot/patches/superdrastic/0001-*`). Those lines of `drastic.cfg` are Banana's, set again
at every start; the rest of DraStic's buttons stay its own menu's.

The other systems have a **Buttons** page too (Settings > Other systems > Buttons; `ra_btn_l2` ... `ra_btn_r3`): L2,
R2, L3 or R3 pressed alone does a job of RetroArch's instead of the game's button (fast-forward on and off, save or
load a state, the menu, pause, a screenshot, nothing; R2 fast-forwards by default). RetroArch's own hotkeys only work
with F held, so `run-retroarch` lists the jobs in `/run/banana/ra-buttons` and moves those buttons' RetroPad binds to
joystick buttons no pad has (30-33; put back after the game), and bananad sends the job's command down RetroArch's
stdin pipe when the button is pressed without F. PlayStation and N64 games keep the four buttons, which they use.

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
  (with the resume state), F + R saves slot 1, F + L loads it. Each button does what its `input.<button>` says
  (`buildroot/patches/bananads/0010-*`, #55, #52): a DS button, or a job (the menu, swap the screens, save or load slot
  1, fast-forward held or on/off, a screenshot, a blow on the mic while held, nothing). L2, R2, L3 and R3's come from
  Settings > Nintendo DS > Buttons (below); the others are the pad as printed unless bananads.conf says otherwise.
- **In-game menu**: F or HOME (or a button set to Menu) pauses the game and opens SuperDrastic's menu, in BananaOS's colours, on both panels: Resume,
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

A card with no microphone keys gets `mic.source = device` and `mic.threshold = 0.05` written into `bananads.conf`
once, so Settings owns them; a card that already names them is left alone. `mic.device` is different: it is not a
Settings row, so the launchers force it to ALSA's `default` on every launch (SuperDrastic hardcodes the same alias in
`src/audio.c`, and `drastic.cfg` has no device key), because only that alias reaches bananad's S16 stereo 48 kHz
capture plug and its resampler. A raw node a card carries from another firmware or an edit over file sharing
(`hw:0`, `plughw:0`, `hw:CARD=…`) has no resampler: it hands the rk817 44.1 kHz against the playback's 48 kHz, the
`symmetric_rate` I2S DAI refuses the capture, the emulator's capture thread logs "no microphone" and exits, and the
game's mic stays silent. `mic.source`/`mic.threshold` are written, not exported: an environment `BANANADS_MIC_*`
wins over the file and would freeze the settings screen's choices. Both launchers also log if
`/run/banana/asound.conf`'s `capture.pcm` is not that plug.

`/data/banana/bananads.env` (shell assignments) adds settings for every run, e.g. `BANANADS_KMS_STATS=600` (the
frame statistics in the log) or `BANANADS_POWER_CPU_LATENCY_US=100` (the cores kept out of deep idle while a game
runs: ~1.5% faster, a battery cost not measured yet). BananaDS needs no BIOS or firmware files (melonDS's FreeBIOS and a
generated firmware).

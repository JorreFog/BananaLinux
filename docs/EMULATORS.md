# What Banana gives an emulator, and what it expects back

This is the contract between the menu and any program that plays a game — on the Retroid Pocket Classic, that is
RetroArch and its cores. Keep to it and the rest of Banana (sleep, the volume keys, the logs, the settings) works
around the emulator without knowing it.

## Starting

`systems.conf` names a `run` command per system, with `%ROM%` the game's path (quoted) and `%SYSTEM%` the system's
id. The commands are the launchers in `/usr/libexec/banana/`: `run-retroarch <core> <rom>`, `run-ps1` (which picks
SwanStation or PCSX-ReArmed from Settings and hands over), and `run-port` for PortMaster.

Before the launcher runs, banana-ui has:

- closed its display (it is no longer DRM master; the panel is free), closed the input devices and the sound
  device;
- set the system's clock profile (`banana-perf`, the `perf` in `systems.conf`);
- written the game's pid to `/run/banana/game.pid`;
- exported `BANANA_ROM` and `BANANA_SYSTEM` (the system's id, what `run-retroarch` reads for its per-system core),
  and, where set by the kernel command line (`banana.top=`, `banana.touch=`), `BANANA_TOP` and `BANANA_TOUCH`.

The launcher runs in its own session (`setsid`), with signals at their defaults and `/data` as the card.

## The hardware, as an emulator sees it

| | |
|---|---|
| The panel | KMS on the `msm` card (`/dev/dri/card1`). **One** 1240×1080 connector, mounted turned (the device tree's `rotation = <90>`), so the framebuffer is 1080×1240 and RetroArch's `video_rotation` turns the content. Take DRM master, set the mode as it is (never change the timing), page-flip on vblank; give it all back on exit. `run-retroarch` forces `video_monitor_index 1`: a single connector is RetroArch's monitor 1, and a card left saying 2 (a two-panel handheld, or the Screen row this build doesn't show) is a black panel with the game running under it. |
| GPU | The Adreno a613 through Mesa's freedreno (A6xx), EGL on GBM, OpenGL ES 3.2. There is no Vulkan driver yet (Turnip is later). Software rendering is fine for the lighter cores. |
| Touch | evdev; the panel's touchscreen is the FT5452, whose device is the one whose sysfs path contains `BANANA_TOUCH`. It reports the panel's own (unrotated) axes, so coordinates need the same turn as the picture. |
| Buttons | evdev, one device: the MCU's `rsinput` gamepad. A = `BTN_EAST`, B = `BTN_SOUTH`, X = `BTN_NORTH`, Y = `BTN_WEST`, d-pad = `BTN_DPAD_*`, L/R = `BTN_TL`/`BTN_TR`, L2/R2 = `BTN_TL2`/`BTN_TR2`, Start/Select = `BTN_START`/`BTN_SELECT`, F = `BTN_MODE`. Read it, never grab it (`EVIOCGRAB`): bananad reads the same device for the power button and the volume keys. The volume keys (`KEY_VOLUMEUP/DOWN`) and F + volume (brightness) are bananad's; ignore them. |
| Sound | ALSA, device `default` (bananad routes it and writes `/run/banana/asound.conf`; the card is the WCD937x codec with the two AW88166 amplifiers through the AudioReach DSP). Three rules on this platform: the route must be set **before** the stream opens (the DSP takes the mixer values at the open), only the first frontend makes sound (so `run-retroarch` re-asserts the route and waits for the menu's stream to be released first), and the device can abort (`SIGABRT`, exit 134) if the DSP is still tearing the menu's graph down — `run-retroarch` reads the substreams' status and waits, and retries the start if it aborts anyway. |
| CPU | Six Cortex-A55 (`policy0`) and two Cortex-A78 (`policy6`); `banana-perf` sets every policy before the game starts. An emulator that lowers a policy's `scaling_max_freq` must put it back on exit. |
| Clock | The kernel's; the time zone is in `TZ`. |

## While it runs

- **Sleep**: a short power press (there is no lid on this handheld) sends `SIGSTOP` to the game's process group,
  then suspends; on waking, `SIGCONT`, within a millisecond of the kernel returning. The emulator sees nothing but a
  gap in time: don't drop audio on a timing jump, and don't treat a vblank that took seconds as an error. The panel
  comes back on by itself (the kernel's DRM resume restores the last framebuffer), so no modeset is needed on wake.
- **Quitting**: a hotkey of RetroArch's own (F + Start; `quit_press_twice` is off). On exit, banana-ui reopens the
  panel and records the play (anything over 5 s). Exit code 0 is a normal quit; anything else is logged.
- **Shutdown while playing**: a long power press or an empty battery sends `SIGTERM` to the process group and waits
  up to 5 s for saves to be written before powering off. Write saves on `SIGTERM`.

## Where things go on the card

| | |
|---|---|
| Games | `/data/roms/<system>` (`folder` in `systems.conf`); `.zip` ROMs are the emulator's to read |
| BIOS files | `/data/bios` |
| Saves and states | `/data/saves/<system>`, `/data/states/<system>` |
| RetroArch's own files | `/data/retroarch` (config, remaps, core options, playlists; `HOME`/`XDG_CONFIG_HOME` point there, never the read-only root) |
| Logs | `/data/banana/logs/retroarch.log` (the one thing worth having after a game that would not start) |
| Screenshots | `/data/screenshots` |

Everything under `/usr` is read-only. `run-retroarch` forces the keys a picture and a sound depend on at every
start (`video_driver`, `video_context_driver`, `input_driver`, `audio_driver`, `audio_device`, `video_monitor_index`
on a one-panel handheld, `midi_driver = null`), because RetroArch writes `retroarch.cfg` back when it quits and one
trip through its own menus otherwise leaves a value the handheld cannot use.

## Settings

Settings > Other systems holds, per system, the core (`core.<system>` in `banana/settings.conf` — another installed
core instead of `systems.conf`'s, e.g. mGBA for the Game Boy Advance for its RetroAchievements), and the shader
(`ra_shader`), audio latency (`ra_audio_latency`) and VSync (`ra_vsync`) RetroArch runs with, plus integer scaling
(`ra_scale`). `run-retroarch` writes them into `retroarch.cfg` before each start; a change made in RetroArch's own
menu afterwards is kept until the setting changes here again. The shader is RetroArch's global preset, a reference
to one of the image's GLSL presets in `/usr/share/libretro/shaders/glsl` (copied to the card they were 238 MB of
small files in exFAT's large clusters and a long first start).

RetroAchievements: the account from the first-time setup (`banana/accounts.conf`) is written into `retroarch.cfg`
as its name, but its password and the token RetroArch gets with it go in a config appended from `/run`, never in
`retroarch.cfg`, which file sharing shows.

## The PlayStation

`run-ps1` picks the core from `core.psx`: **SwanStation** (DuckStation's libretro core, the default) or
**PCSX-ReArmed**. SwanStation draws its 3D on the CPU through its software renderer — on this handheld that is
faster than the freedreno OpenGL path for now, and glitch-free (there is no Vulkan driver, so its Vulkan backend is
unavailable). PCSX-ReArmed is the ari64 dynarec with its NEON rasterizer, the lighter of the two. Both scale a
320×240 frame to a clean integer multiple inside the landscape view (960×720 at 3x); RetroArch's integer scaler is
off for SwanStation, which fills its own 4:3 viewport. A core override saved from RetroArch's own menu can't take
the panel settings away: `run-ps1` drops `video_monitor_index` and `stdin_cmd_enable` from it.

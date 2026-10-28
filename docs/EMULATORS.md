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
| The panel | Through **cage** (a kiosk Wayland compositor: one application, fullscreen). cage takes DRM master on the `msm` card (`/dev/dri/card1`), sets the panel's mode as it is (never the timing) and page-flips on vblank, and it turns the output with the panel: the device tree's `rotation = <90>` becomes the connector's "panel orientation", which wlroots reads. RetroArch is a Wayland client, so **everything** it draws -- the game, its menu, the OSD, the widgets -- is composed in the landscape shape the user is looking at and turned once, by cage, on the way to the panel. `run-retroarch` therefore forces `video_rotation 0`; there is nothing above cage that wants a rotation. |
| GPU | The Adreno a613 through Mesa's freedreno (A6xx), EGL on GBM, OpenGL ES 3.2, and **Turnip** (Mesa's freedreno Vulkan; Vulkan 1.0 on this part, the a6xx gen1 low block having no hardware multiview). SwanStation's Vulkan renderer runs at 60 fps at 5x through it. Software rendering is fine for the lighter cores. |
| Touch | evdev; the panel's touchscreen is the FT5452, whose device is the one whose sysfs path contains `BANANA_TOUCH`. It reports the panel's own (unrotated) axes, so coordinates need the same turn as the picture. |
| Buttons | evdev, one device: the MCU's `rsinput` gamepad. A = `BTN_EAST`, B = `BTN_SOUTH`, X = `BTN_NORTH`, Y = `BTN_WEST`, d-pad = `BTN_DPAD_*`, L/R = `BTN_TL`/`BTN_TR`, L2/R2 = `BTN_TL2`/`BTN_TR2`, Start/Select = `BTN_START`/`BTN_SELECT`, F = `BTN_MODE`. Read it, never grab it (`EVIOCGRAB`): bananad reads the same device for the power button and the volume keys. The volume keys (`KEY_VOLUMEUP/DOWN`) and F + volume (brightness) are bananad's; ignore them. In a game a volume press is handed to RetroArch through `/run/banana/retroarch.cmd` (`VOLUME_UP`/`VOLUME_DOWN`) so it changes its own volume live and shows its OSD; only the menu edits the card's mixer. |
| Sound | ALSA, device `default` -- which is PipeWire's plugin now (the image's sound server; bananad routes the card and publishes the route, and `rcS` writes it and the codec's levels before PipeWire starts). The card is the WCD937x codec with the two AW88166 amplifiers through the AudioReach DSP. Two rules on this platform: the route must be set **before** the device is opened (the DSP takes the mixer values at the open), and there is no UCM profile (routing is by `amixer`). The server owns the card's single frontend, so the menu and a game no longer take turns at it; the ALSA abort (exit 134) that `run-retroarch` retries round is now only possible on a card with no PipeWire. |
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
**PCSX-ReArmed**. SwanStation's renderer is its own option; Banana writes **Vulkan at 5x** (the panel's own 1080p),
which runs at 60 fps through Turnip -- where its software renderer and the freedreno OpenGL path both fell short.
PCSX-ReArmed is the ari64 dynarec with its NEON rasterizer, the lighter of the two. RetroArch's integer scaler is
off for SwanStation, which fills its own 4:3 viewport. A core override saved from RetroArch's own menu can't take
the panel settings away: `run-ps1` drops `video_monitor_index` and `stdin_cmd_enable` from it.

`run-retroarch` gives a core the video driver its renderer needs: a libretro core's hardware context comes from the
video driver, so SwanStation-on-Vulkan gets `video_driver = vulkan`, and every other core keeps `gl`. The *context*
driver is the Wayland one, because the game runs under cage: `vk_wayland` for Vulkan (the Wayland surface Turnip's
`VK_KHR_wayland_surface`) and `wayland` for GL, in place of the `khr_display`/`kms` pair it used when it took the
panel itself. That also means the vulkan-loader must be built with Wayland WSI (`BUILD_WSI_WAYLAND_SUPPORT`) and
Mesa with the `wayland` platform, or the instance has no `VK_KHR_wayland_surface` and RetroArch gives up on the
video driver and exits -- the game is back in the menu before it starts.

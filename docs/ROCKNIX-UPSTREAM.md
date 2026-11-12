# What to ask ROCKNIX for

One of BananaOS's two routes to the AYN Thor is ROCKNIX's SM8550 image with BananaOS's menu as its frontend
(docs/THOR-ROCKNIX.md).
Today `tools/rocknix-banana.sh` edits a ROCKNIX checkout to get there. Each edit below is a place where ROCKNIX could
offer a hook instead, so a frontend like ours would be an add-on package rather than a fork. This page is the text for
that conversation: an issue (or a discussion) on ROCKNIX/distribution, one point per heading, each with what we do now.

Nothing here has been sent. It is a draft for the maintainers of this repository to post when they choose.

---

## Draft: "A frontend other than EmulationStation, per device"

Hi! We're building BananaOS's dual-screen menu as a frontend for ROCKNIX on the AYN Thor: ROCKNIX's SM8550 image,
kernel, emulators and settings as they are, with our menu (both screens as two Wayland windows under your sway) in
place of EmulationStation. It works today with a handful of edits to a checkout. We'd rather not keep a fork, and
we think other frontends would like the same hooks. Here is what we'd need, smallest first. We're happy to send the
PRs.

### 1. Choosing the frontend

**What we do:** an autostart script in `/usr/lib/autostart/common/` rewrites `UI_SERVICE` (from `090-ui_service`'s
`sway.service essway.service` to `sway.service bananasway.service`), and a setting (`banana.frontend=0`) goes back to
EmulationStation.

**What would help:** a documented setting, e.g. `system.frontend=<service>`, that the autostart honours, with
EmulationStation the default. A frontend package then only installs its service.

### 2. Emulators from an add-on package

**What we do:** add `add_emu_core <system> <emulator> <core> false` lines to
`packages/virtual/emulators/package.mk` (just before each `add_es_system`), and our packages to the device's
`ADDITIONAL_PACKAGES`.

**What would help:** a drop-in folder read by `mk_es_systems` (e.g. `config/emulators.d/*.conf` with the same
`system|emulator|core|default` lines your build already writes to `${ESTMP}`), so a package can add an emulator to a
system without touching the list. They'd never be the default unless the device says so.

### 3. Options from an add-on package

**What we do:** insert `<core>` and `<emulator>` blocks into `es_features.cfg`.

**What would help:** `es_features.d/*.cfg` merged at build time (or by EmulationStation at start). Our menu reads the
same file to show each game's options, so any frontend benefits.

### 4. Sleep without EmulationStation's API

**What we do:** nothing yet. `rocknix-fake-suspend` asks EmulationStation's API (`localhost:1234/runningGame`)
whether a game runs, and shuts down through it. With another frontend the API is down, so it assumes a game is
running and calls `shutdown now`, which is fine but loses the "save metadata" path.

**What would help:** the running game as a file (`/run/rocknix/running-game`, written by `runemu.sh` around the
game), and a shutdown that stops `${UI_SERVICE}` cleanly before powering off. Then any frontend gets the same sleep
and timed shutdown.

### 5. The bottom screen during a game

**What we do:** our menu keeps its bottom window for its own game panel and closes the top one, and we steer the
game's window to the top output. We send RetroArch commands to its network command port, which `runemu.sh` already
turns on for two-screen devices (thank you, that made it easy).

**What would help:** an environment variable or setting that tells `runemu.sh` a frontend owns the second screen,
so it skips `lowerdeck`'s proxy and the `vertical-check` rule. Also, the `for_window [title=".*Bottom.*"]` rule is
great: documenting it as the way to place a second window would let emulators rely on it.

### 6. Small ones

- `add_es_system` reading `SYSTEM_COMMAND` from `config/emulators/<system>.conf` is how a frontend can see a system's
  own command. A short note in the docs would help other frontends.
- Our scripts write ROCKNIX's settings through `set_setting`. A `set_setting` that takes a file override (we use
  `ROCKNIX_SETTINGS` in our tests) would make frontends' tests easier.

---

## Where each point lives in this repository

| Point | Our side |
|---|---|
| 1 | `rocknix/packages/ui/banana-frontend/sources/099-banana-frontend`, `package.mk` (`UI_SERVICE` in `profile.d`) |
| 2 | `tools/rocknix-register.py` (`add_cores`), `ADDITIONAL_PACKAGES` |
| 3 | `tools/rocknix-register.py` (`add_features`), `system/rootfs/usr/libexec/banana/rocknix-options` |
| 4 | `system/rootfs/usr/libexec/banana/handheld poweroff-after` (we only set the delay) |
| 5 | `ui/src/kms_sdl.cpp` (`leaseSdl`), `system/rootfs/usr/libexec/banana/ra-netcmd`, `run-rocknix` |
| 6 | `system/rootfs/usr/libexec/banana/run-rocknix`, `handheld`, `rocknix-options` |

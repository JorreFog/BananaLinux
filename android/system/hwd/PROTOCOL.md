# banana-hwd's protocol

`banana-hwd` owns the AYN Thor's own hardware on Android: the fan, the stick lights, the CPU and GPU clock profiles,
both panels' backlights, the Home (AYN) button and the statistics the quick menu shows. It does what BananaOS's Linux
tools did (`usr/libexec/banana/handheld`, `sysstat`, `usr/bin/banana-perf`, the menu's Home button handling), in one
small native process. The launcher (`android/launcher`) is its client.

## Connecting

- A Unix stream socket, `/dev/socket/banana-hwd` (made by init, `banana-hwd.rc`). From Java/Kotlin:
  `LocalSocket` with `LocalSocketAddress("banana-hwd", LocalSocketAddress.Namespace.RESERVED)`.
- Who may connect is SELinux's decision (`sepolicy/private/banana_hwd.te`): `platform_app`, `priv_app`,
  `system_app` and `shell`. The launcher must be a privileged or platform-signed app; an ordinary app is refused.
- Up to 16 clients at once. A client that stops reading is dropped, never waited for.

## Lines

Everything is UTF-8 text, one line per message, `\n` at the end (`\r\n` accepted). Words are separated by spaces;
command words are case-insensitive, values are not (`FAN MODE quiet`).

A request is one line; its reply is exactly one line:

```
OK[ <payload>]
ERR <code>[ <detail>]
```

`<code>` is one of `unknown` (no such command), `badarg` (a value it doesn't take), `unsupported` (this kernel or
this handheld doesn't have it), `failed` (the kernel refused the write), `toolong` (a line over 511 bytes: the
connection is closed), `busy` (17th client: closed).

Replies come in the order requests were sent. Payloads of `GET`-type commands are `key=value` words separated by
spaces; a key the kernel gives no value for is left out (never empty), so a client reads what is there and shows the
rest as unknown.

After `SUBSCRIBE`, the connection also receives event lines at any time, between replies:

```
EVENT <source> <what>
```

A client that both subscribes and sends requests must tell `EVENT` lines from replies by their first word. The
launcher is expected to keep one connection subscribed for events and use another (or the same, with that care) for
requests.

## Commands

| Request | Reply | What it does |
|---|---|---|
| `PING` | `OK pong 1` | the protocol version is the last word (1) |
| `VERSION` | `OK 1` | |
| `CAPS` | `OK <words>` | what this handheld has: `fan`, `fan_trips` or `fan_pwm` (which fan control, below), `leds`, `perf`, `gpuboost`, `backlight_top`, `backlight_bottom`, `temps`, `home` |
| `SUBSCRIBE` / `UNSUBSCRIBE` | `OK` | events on this connection, or not |
| `STATS` | `OK cpu=.. cpu_mhz=..` | sysstat's values (below). The first call after start has no `cpu=` (nothing to compare with). Cheap enough once a second |
| `FAN` (or `FAN GET`) | `OK mode=quiet backend=trips pwm=60 pct=23 state=2 max_state=8 trips=32 manual_pct=50 soc_c=47` | the profile, the fan's drive (pwm 0-255, pct 0-100), the cooling device's state, the mean CPU/GPU temperature |
| `FAN MODE <profile>` | `OK` | `stock`, `quiet`, `moderate`, `aggressive`, `max`; on `fan_pwm` also `off` and `manual`. Kept across boots |
| `FAN SET <0-100>` | `OK` | a fixed speed (`fan_pwm` only: `ERR unsupported` on `fan_trips`). Mode becomes `manual` |
| `FAN TRIPS` | `OK <zone>:<trip>=<now>/<boot> ...` | the trips bound to the fan (`fan_trips`): for a details page and for debugging |
| `LEDS` (or `LEDS GET`) | `OK mode=system color=ffbe00 level=140 effect=none system=psx rings=8` | |
| `LEDS MODE <mode>` | `OK` | `off`, `battery` (green; amber while charging; red under 20 %), `system` (the playing system's colour, below), `color` |
| `LEDS COLOR <c>` | `OK` | `rrggbb` (or `#rrggbb`) or a name: yellow red green blue purple pink cyan white amber. Sets mode `color` |
| `LEDS LEVEL <l>` | `OK` | `low` (64), `medium` (140), `high` (255) or 0-255 |
| `LEDS EFFECT <e>` | `OK` | `none` or `breathe` (a 2 s pulse, done by the daemon) |
| `LEDS SYSTEM <id>` | `OK` | the system now playing, by its `systems.conf` id: its maker's colour when mode is `system` (Nintendo red, Sega blue, Sony cyan, NEC purple, arcade pink, the rest the banana's yellow) |
| `PERF` (or `PERF GET`) | `OK profile=balanced gpu_boost=0 policy0=walt:2016/2016 policy3=..` | each cluster's governor and max/top clock in MHz |
| `PERF SET <profile>` | `OK` | `ui`, `powersave`, `balanced`, `steady`, `performance`, `superdrastic` (below). Kept across boots |
| `GPUBOOST` | `OK off available=0` | |
| `GPUBOOST on\|off` | `OK` | the GPU's top clock raised (where the kernel's table has it, else `ERR unsupported`) |
| `BACKLIGHT top\|bottom` | `OK <value> <max>` | a panel's backlight, raw |
| `BACKLIGHT top\|bottom <value>` | `OK` | written raw, not kept (below) |
| `GAME START <system> [<profile>]` | `OK` | at a game's start: `LEDS SYSTEM <system>` and `PERF SET <profile>` (the system's `perf` from systems.conf) in one |
| `GAME END` | `OK` | at its end: `PERF SET ui` |
| `SUSPEND` / `RESUME` | `OK` | the screens went off / on: the lights off (lit, they keep the SoC awake; ROCKNIX does the same), the fan stopped on `fan_pwm`; back after |
| `QUIT` | `OK bye` | closes the connection |

### Events

| Line | When |
|---|---|
| `EVENT home down` | the Home (AYN) button went down |
| `EVENT home tap` | it came up before the hold time (3 s): **the menu over the game, or back into the game** |
| `EVENT home hold` | still down at 3 s: **the quick menu**, sent while the button is held (not at release) |
| `EVENT home release` | it came up after a hold (the quick menu is already up; nothing more to do) |

The daemon only reports; what a tap or a hold does is the launcher's. It never grabs the device, so the focused app
(the game) also receives the key (`KEYCODE_F24`, which no emulator binds by default).

The button is `KEY_F24` on `gpio-keys` (`hwd.conf`: `home_keys`, `home_devices`). The pad's own keys are separate:
its `KEY_HOME` (102) is already Android's HOME (hardware/ayn's `Vendor_045e_Product_028e_Version_1234.kl`), which
brings the launcher to the front by itself, and its `BTN_MODE` (316) is `KEYCODE_BUTTON_MODE`, delivered to the game.

### STATS keys

As BananaOS's `sysstat`:

| Key | Meaning |
|---|---|
| `cpu` | percent busy since the last `STATS` call |
| `cpu_mhz` | the fastest core's clock |
| `cpu_c`, `gpu_c` | the hottest CPU zone and GPU zone, °C |
| `gpu` | GPU busy percent (`kgsl-3d0/gpu_busy_percentage`; mainline msm has no such file) |
| `gpu_mhz` | the GPU's clock |
| `mem_used_mb`, `mem_total_mb`, `mem_pct` | memory in use: total less available |
| `power_state` | Charging, Discharging, Full, Not charging |
| `battery` | percent |
| `power_mw` | into the battery positive, out of it negative |
| `fan_pct` | the fan's drive, 0-100 |

On LineageOS a key is missing wherever SELinux keeps the daemon from a node whose vendor label isn't granted yet
(`sepolicy/vendor/banana_hwd.te` says which and how to add them): until the first boot's denials are read, expect no
`gpu`/`gpu_mhz`. The launcher can always take the battery from `BatteryManager` (`BATTERY_PROPERTY_CAPACITY`,
`BATTERY_PROPERTY_CURRENT_NOW` and the sticky `ACTION_BATTERY_CHANGED`'s voltage) and memory from
`ActivityManager.getMemoryInfo()`, which need no permission.

## How each piece works

### Fan: two backends

- **`fan_trips`** (AYN's kernel, which LineageOS uses). The fan is a thermal cooling device (`type` `pwm-fan`,
  `max_state` 8) that the kernel's `step_wise` governor drives from several zones; `pwm1` written by hand is undone
  at the next evaluation. So a profile moves the **trip points bound to the fan**, the way
  [thor-fanctl](https://github.com/Wingdom/thor-features) (Apache-2.0) does, with its safety rules: a CPU/GPU trip
  is only ever lowered from its boot value and never under 40 °C, and `usb-therm`'s first fan trip (43 °C at boot,
  inherited from Qualcomm's reference design: the fan ran at idle while charging) goes to 60 °C within 35-70 °C.
  Every trip is written and read back on each change, which also makes each zone evaluate again (they don't poll).
  The boot values are recorded once per boot (`/data/misc/banana/hwd.state.trips`, keyed by the boot id), so a
  restarted daemon doesn't mistake its own moved trips for them. No `off` and no fixed speed: the kernel keeps the
  fan, and every trip stays.

  | Profile | usb-therm | CPU/GPU fan ladder |
  |---|---|---|
  | `stock` | as at boot | as at boot |
  | `quiet` (default) | 60 °C | as at boot |
  | `moderate` | 60 °C | 5 °C sooner |
  | `aggressive` | 60 °C | 10 °C sooner |
  | `max` | 60 °C | 30 °C sooner, floored at 40 °C: runs from 40 °C |

- **`fan_pwm`** (mainline with ROCKNIX's device tree). Nothing maps a zone to the fan, so the daemon runs ROCKNIX's
  SM8550 curve on `pwm1` every 3 s from the mean of the `cpu*` and `gpu*` zones (`handheld`'s `fan_loop`), plus
  `off`, `manual` and `FAN SET`; above 90 °C it goes to full whatever the profile.

### Lights

AYN's kernel names one LED per ring segment, `left:stick:0`-`3` and `right:stick:0`-`3`, and takes
`multi_intensity` as `R G B` and `brightness` 0-255 (LineageOS's `hardware/ayn`: AynParts' `LightUtils`,
`services/charger_rgb`). Mainline with ROCKNIX's device tree names them `rgb:l1`-`rgb:r4`, wired `B G R`. Both are
found by looking; `hwd.conf` overrides.

LineageOS's own **AynParts** (Settings > AYN, `org.lineageos.settings.ayn`) also writes the lights' colour when its
"RGB lighting" is enabled (off by default), and restores it at boot. With both, the last writer wins: the launcher
should be the only place the lights are set (the ROM can remove AynParts, or leave its RGB switch off).

### Clock profiles

`banana-perf`'s profiles, per cluster (policy0: 3x A510, policy3: 2x A715 + 2x A710, policy7: X3), each capped at a
share of its own top clock: `powersave` 71 %, `steady` a 55 % floor, `performance`/`superdrastic` the
`performance` governor, the others the full range. Where a governor isn't offered (AYN's kernel has `walt`, not
`schedutil`), the cluster keeps the governor it booted with. The GPU gets `performance` for `performance` and its own
governor otherwise (the Linux image's `powersave` GPU for the menu is not carried over: Android's launcher is drawn
by the GPU).

On a Qualcomm Android build the vendor power HAL (`vendor.qti.hardware.perf`) also moves these limits for its boosts
(app launch, touch). The daemon's caps are what it sets last; a boost may lift them for its duration.

### Backlights

`panel0-backlight` is the top (6") panel and `panel1-backlight` the bottom (3.92") one on AYN's kernel. On Android the
framework owns brightness (each display's brightness setting, through the display HAL), so `BACKLIGHT ... <value>`
is a raw override that the framework's next change replaces, and it isn't kept. The launcher should set brightness
the Android way (the `Settings.System.SCREEN_BRIGHTNESS` of the default display, and per-display brightness through
`DisplayManager` where the build allows), and use the daemon to read the raw levels or where that way doesn't reach
the bottom panel.

### Sleep

Android suspends by itself. The launcher sends `SUSPEND` on `ACTION_SCREEN_OFF` and `RESUME` on `ACTION_SCREEN_ON`.

## Configuration and files

| Path | What |
|---|---|
| `/system_ext/bin/banana-hwd` | the daemon |
| `/system_ext/bin/banana-hwc` | a command-line client: `adb shell banana-hwc STATS`, `banana-hwc -e 4` prints 4 events |
| `/system_ext/etc/banana/hwd.conf` | node overrides (`hwd.conf` here lists every node, measured or not) |
| `/data/misc/banana/hwd.state` | the settings kept across boots |
| `/data/misc/banana/hwd.state.trips` | the fan trips' boot values, for this boot |
| `/dev/socket/banana-hwd` | the socket |

## Testing without a Thor

`sh android/system/hwd/tests/hwd-test.sh` builds the daemon and the client with the host's compiler and runs every
command above against two made-up `/sys` trees (AYN's kernel and mainline) and a pipe standing in for the button's
input device. `BANANA_SYS`, `BANANA_PROC`, `BANANA_DEV` and `BANANA_HWD_STATE` move the roots, as BananaOS's shell
tools' tests do.

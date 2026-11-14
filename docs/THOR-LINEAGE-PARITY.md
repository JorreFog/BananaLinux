# BananaOS on LineageOS: what the Linux image does, and how the Android build does it

The owner's aim: a stripped LineageOS for the AYN Thor with exactly the features of BananaOS's own Linux image
(`docs/THOR-OS.md`) and BananaOS's menu as the launcher. This page goes through the Linux image feature by feature
and gives each its Android route and its state. How the ROM is built is `docs/THOR-LINEAGE.md`; the launcher is
`android/launcher`; the system layer this page describes is `android/system/`.

**Nothing here has run on a Thor or in a LineageOS build.** What was checked without one is listed at the end.

States:

| State | Meaning |
|---|---|
| **done here** | written in `android/system/` and checked as far as a PC allows (compiled, tested against stand-ins, or read in the upstream source) |
| **needs ROM** | a change in the ROM's makefiles, device tree or kernel (`android/product`, `android/device`) |
| **needs launcher** | the launcher app has to do it, from what `android/system` gives it |
| **unverified** | the route is known and written, but rests on something nobody here has run |
| **not possible** | no route on Android that keeps the feature as it was; what comes closest is said |
| **owner installs** | works, but the app can't be put in the image (licence or no pinnable download) |

## The system layer at a glance

| Piece | Where | What it is |
|---|---|---|
| Launch table | `android/system/launch.conf` | how the launcher starts a game of each of the 63 entries of `buildroot/board/thor/systems.conf` (and 4 alternatives): package, activity, action, data, extras, each read in the emulator's source |
| Emulators | `android/system/emulators/` | `Android.bp`: 11 `android_app_import` modules (`banana-<emu>`), the phony `banana_emulators` (10 of them) and `banana_emulators_extra` (Winlator); `fetch.sh`: the APKs at pinned versions with SHA-256 |
| Hardware daemon | `android/system/hwd/` | `banana-hwd` (C, `cc_binary` on /system_ext, `banana-hwd.rc` as its `init_rc`), its client `banana-hwc`, `hwd.conf`, `PROTOCOL.md`, `tests/hwd-test.sh` |
| SELinux | `android/system/sepolicy/` | `public/` (the domain type), `private/` (the rules, file contexts), `vendor/` (vendor-labelled nodes) |
| Properties | `android/system/props/banana.prop` | USB debugging on by default |

## Emulators (THOR-OS.md "What is in it", THOR-EMULATORS.md)

Linux: RetroArch with 49 cores, and Dolphin, ARMSX2, Azahar, PPSSPP, Vita3K, Flycast and ScummVM standalone, DraStic
or BananaDS for the DS; 54 systems. Android: the same emulators' Android builds, from their projects.

### In the image (`banana_emulators`, `fetch.sh`)

Each APK was downloaded from the URL in `fetch.sh` on 2026-11-14, hashed, its manifest read (package, version, ABIs),
and deleted.

| Module | App | Version | Package | Licence | SHA-256 | Size |
|---|---|---|---|---|---|---|
| banana-retroarch | RetroArch (buildbot stable) | 1.22.2 | com.retroarch.aarch64 | GPL-3.0 | `7bd5d208dfe93cc8e2ea6c04608948ce1a045980f160a58ca2d0993aa20ad213` | 184 MB |
| banana-dolphin | Dolphin | 2609b | org.dolphinemu.dolphinemu | GPL-2.0+ | `9171e6d39a8d691e5a1fe1e518c56f909768fa837ee21437a06241f87f4b5130` | 22 MB |
| banana-ppsspp | PPSSPP | 1.20.4 | org.ppsspp.ppsspp | GPL-2.0+ | `dd702a31270ecd68db37afee311d6efe08b1d4da9601d2750ce1a73bbd316c3f` | 45 MB |
| banana-scummvm | ScummVM (arm64-v8a) | 2026.3.0 | org.scummvm.scummvm | GPL-3.0+ | `26e25a59496eb94a4dc76f282bb3234d6d36f7f393c3e699e0a62e1a5c3e412b` (= the published .sha256) | 130 MB |
| banana-flycast | Flycast | 2.7 | com.flycast.emulator | GPL-2.0 | `1ac54d77b8283f6d58d077396d0916ec12c15b7e4ada9d957836a64590756c00` | 36 MB |
| banana-azahar | Azahar (vanilla) | 2126.2 | org.azahar_emu.azahar | GPL-2.0+ | `b511d1d38012c5ab0dfdb4479335059bb89ef17acfbf0ba45e79107ed4185ba3` | 50 MB |
| banana-melonds | melonDS Android | 1.10.0 beta | me.magnum.melonds | GPL-3.0 | `fea43a3727b4de0a384eeb00e77a6cb87ae9aaab9ba28a913e6d73ea997d1d8a` | 20 MB |
| banana-vita3k | Vita3K | build 4338 (2c85515d) | org.vita3k.emulator | GPL-2.0 | `839c5cc78eddd55cbf3efe0ca3528ab0e4e3e47664f9eebc825287ae4389ae1e` | 30 MB |
| banana-armsx2 | ARMSX2 (Android 15+ build) | 2.9.3 | com.armsx2 | GPL-3.0+ | `0ac4d6d1e92dade794b632d488e5f468ba1e18a09df751d05bd3e836c31182f7` | 78 MB |
| banana-gamenative | GameNative | 1.4.0 | app.gamenative | GPL-3.0 | `51e7edc667a352c664f7da274d15b973cd054ec04e0b828e2039fb40c334224d` | 571 MB |
| banana-winlator (extra) | Winlator | 11.2 | com.winlator | LGPL-2.1 | `ea5c81bf0e5b90a6b9ccb8007121ab6738c8646a5a60b341ce0ac2b33f1d100a` | 158 MB |

About 1.2 GB on /product for `banana_emulators` (GameNative is half of it), 1.3 GB with Winlator. All are 64-bit
(arm64-v8a present in every APK), which the ROM's 64-bit-only zygote needs. They keep their projects' signatures,
so each updates from its own channel.

Notes:

- **RetroArch's cores are not in the APK.** RetroArch's Online Updater fetches them (every core the launch table names
  was on buildbot's `nightly/android/latest/arm64-v8a` on 2026-11-14). For a first boot with no network, the launcher
  can hand core files it ships or downloads to RetroArch's exported `CoreSideloadActivity` (1.22.2; debug builds only
  at RetroArch's master), which copies each into `/data/data/com.retroarch.aarch64/cores/`. **needs launcher** (or the
  first-time setup step "download the cores").
- **BananaOS's own cores** (BananaNES, BananaSNES, BananaGBA) have no Android build: FCEUmm, Snes9x and mGBA, which
  they are made from, stand in. Their saves are the same format; the Banana-specific options are not there.
  **not possible** without porting the cores (they are libretro cores: an NDK build of each is possible later).
- **Vita3K** publishes only rolling builds: "4338" is a build number of `Vita3K/Vita3K-builds`, which keeps them.
- **ARMSX2** ships four builds per release; the `a15` one (Android 15+, armv8.2 with FP16 and dot product) is the one
  for LineageOS 24 on the 8 Gen 2.

### Owner installs

| App | Why not in the image | Route |
|---|---|---|
| DraStic (com.dsemu.drastic) | paid, closed source | Play Store; `launch.conf` `[nds.drastic]` starts it (activity and intent from ES-DE's configuration, unverified against DraStic) |
| MAME4droid 2024 (com.seleuco.mame4d2024, GPL-2.0) | its GitHub release assets couldn't be listed from here (GitHub's web and API were closed to this session) to pin a file and hash | its GitHub release or Play; `[gameandwatch.mame4droid]` |
| BIOS and firmware files (PS1, PS2, Saturn, Mega-CD, Neo Geo, the Vita's firmware, the DS's optional ones) | not redistributable | the card, as on Linux; each emulator's own setting points at them (**needs launcher** for a first-time step that sets each) |
| DuckStation | its licence forbids repackaging | not used (SwanStation's core instead, as on Linux) |

### System by system

`launch.conf` covers every id of `systems.conf` (63: 55 game systems including PC games, and 8 of BananaOS's own pages and apps) plus
4 alternatives (`nds.drastic`, `gameandwatch.mame4droid`, `scummvm.standalone`, `pc.winlator`).

| Systems | Linux | Android | State |
|---|---|---|---|
| 41 systems on RetroArch cores (NES ... DOS) | RetroArch on KMS, 49 cores | RetroArch's `RetroActivityFuture` with `ROM`, `LIBRETRO`, `CONFIGFILE` extras, the same cores (Android names: `mupen64plus_next_gles3`, ...) | **done here** (table); **unverified** on a Thor |
| GameCube, Wii | Dolphin nogui | Dolphin's `MainActivity`, the game as a content URI (and the `AutoStartFile` path) | **done here**, **unverified** |
| Nintendo DS | DraStic (SuperDrastic) or BananaDS, both screens drawn by the emulator | melonDS (both screens in one window), DraStic as the owner's alternative | **done here**; SuperDrastic's per-game clock governor and BananaDS: **not possible** (Linux programs) |
| Nintendo 3DS | Azahar, its two windows on the two panels | Azahar's `EmulationActivity`, content URI | **done here**; the bottom screen on the Thor's bottom panel: **unverified** (Azahar's own layout options; Android gives an app one display unless it uses `Presentation`) |
| Game & Watch | MAME (mame-gnw) with artwork | RetroArch's `mamemess` core, or MAME4droid (owner installs) | **unverified** (that the core carries the G&W drivers and matches the romsets) |
| PlayStation | SwanStation | the same core | **done here** |
| PlayStation 2 | ARMSX2 (SDL front end) | ARMSX2 Android, content URI | **done here**, **unverified** |
| PSP | PPSSPP | PPSSPP, content URI | **done here** |
| PS Vita | Vita3K, `.psvita` files with title IDs | Vita3K's `Emulator` with `title_id`; installs from Vita3K's menu | **done here**; installing a `.vpk`/`.pkg` from the shelf: **not possible** by intent (Vita3K's own menu) |
| Dreamcast, Naomi, Atomiswave | Flycast | Flycast, content URI | **done here**; Flycast opening another app's content URI: **unverified** |
| PICO-8 | official PICO-8 or fake-08 | retro8 core (no fake-08 core and no PICO-8 for Android) | **unverified** (fewer carts) |
| ScummVM | ScummVM standalone, `.scummvm` files | RetroArch's ScummVM core (takes the same `.scummvm` file); standalone ScummVM only by its own game list (`scummvm:<target>`) | **done here** |
| PC games | Box64 + Wine, Steam via DepotDownloader | GameNative (below) | **done here** (route); **needs launcher** (the hub) |
| Wii U, PS3, Switch, Xbox, Xbox 360 | left out | left out here too (no change of policy; Android builds of some exist, none was asked for) | as on Linux |

### RetroAchievements (THOR-EMULATORS.md)

Linux: one login, written into each emulator's settings for the length of a game (`ra-standalone`). Android: each
app's settings are in its own private storage, which another app can't write. RetroArch takes a config file
(`CONFIGFILE`), so the launcher can keep RetroArch's `cheevos_username`/`cheevos_token` in the config it passes
(**needs launcher**). The standalone emulators each log in from their own menus. **not possible** as one login.

## PC games and Steam (THOR-OS.md "PC games")

Linux: Box64 + Wine 11 + DXVK + VKD3D-Proton fetched to the card; the owner's Steam library through DepotDownloader
(login by QR code or password, downloads to `roms/pc/<Game>`), a hub with Play/Download/Compatibility per game.

**Our own route doesn't run on Android.** DepotDownloader's `linux-arm64` build (3.4.0, the one `usr/libexec/banana/steam`
pins, downloaded again and inspected here) is a glibc program: its ELF interpreter is `/lib/ld-linux-aarch64.so.1`
and it needs `libc.so.6`, `libstdc++.so.6`, `libgcc_s.so.1`. Android has bionic and no such loader, so it can't start
there (verified from the binary, not by running it on Android). Wine, Box64 and DXVK as the Linux image fetches them
are glibc builds too. Running them would mean a glibc userland inside the ROM, which is what GameNative and Winlator
already carry.

**The Android route: GameNative** (github.com/utkarshdalal/GameNative, GPL-3.0, package `app.gamenative`):

- **Steam login**: by QR code (its `LoginScreen.QR`, JavaSteam's `beginAuthSessionViaQR` in `SteamService.kt`) or
  account name, password and Steam Guard: the same two ways as the Linux image. Also Epic, GOG and Amazon.
- **Library and downloads**: its own (JavaSteam's depot downloader, `service/download/`), into its own storage, with
  progress; then it runs the game in a Wine container (Box64 or FEX, DXVK/VKD3D, Turnip).
- **Started per game from another app**: yes. `gamenative://run?appid=<Steam app id>&gamesource=STEAM` with
  `ACTION_VIEW`, or the action `app.gamenative.LAUNCH_GAME` with the int extra `app_id` and `game_source`
  (`utils/IntentLaunchManager.kt`, v1.4.0). `launch.conf`'s `[pc]` uses the URI form, the app id read from the
  `.pc` file's `steam = <id>` line, the Linux image's own format. A game not yet downloaded: GameNative's own
  flow takes over (**unverified** what it shows: likely its download page).
- Redistributable: yes (GPL-3.0 APK from its releases), so it is in `banana_emulators`. It is 571 MB.

What maps how:

| Linux hub | Android | State |
|---|---|---|
| Steam library on the PC shelf with Steam's covers | the launcher lists the library itself from the `.pc` files, or from Steam's web API with the owner's key; GameNative doesn't export its library to other apps | **needs launcher** (the list); **not possible** to read GameNative's list |
| Download with progress in the list | GameNative's own download screen | **not possible** inside our menu: it happens in GameNative |
| Play | `[pc]` intent | **done here** |
| Compatibility, Overlay, Direct3D, which program | GameNative's per-game container settings (its own screens; `container_config` JSON extra exists for a one-off override) | **needs launcher** if wanted from our menu, via `container_config` (**unverified**) |
| Games copied to the card by hand | imported as custom games in GameNative (or Winlator shortcuts) from their own menus | **not possible** from the shelf by intent alone (GameNative's `exe=` matches only a container it already has; Winlator's game activity isn't exported) |
| Steam's own Windows client in the prefix | GameNative runs games without it; a game needing Steam's DRM uses GameNative's Steam emulation options | as GameNative does |

**Winlator** (11.2, LGPL-2.1, `banana_emulators_extra`): upstream Winlator's `XServerDisplayActivity` is not
exported, so our launcher can only open Winlator, not a game in it. It is the owner's tool for setting up games by
hand.

## The Home button (THOR-OS.md "The Home button")

Linux: a press freezes the game (cgroup freezer) and shows the menu on both screens; again, the same frame. Held 3 s:
the quick menu on the bottom screen over the running game.

| Part | Android route | State |
|---|---|---|
| Which button | `KEY_F24` on `gpio-keys` (the "AYN key" under the bottom screen; measured by thor-features' `thor-aynkey`). The Linux profile's `HW_HOME_KEY=316` was the pad's `BTN_MODE`, a different key | **done here** (`hwd.conf`) |
| Reading it while a game has focus | `banana-hwd` reads the input device itself (no grab) and sends `EVENT home tap` / `EVENT home hold` (at 3 s, while held) to subscribed clients | **done here** (tested with a stand-in device) |
| Tap: the menu over the game | the launcher brings its activity to the front; the game's activity is paused (`onPause`/`onStop`) and stays in memory. No freezer: an emulator keeps running while paused unless it pauses itself (RetroArch, Dolphin, PPSSPP, Azahar, Flycast pause in `onPause`; **unverified** each) | **needs launcher** |
| Tap again or the same game: back to the same frame | the launcher moves the game's task to the front (`ActivityManager.moveTaskToFront` or relaunching with `FLAG_ACTIVITY_REORDER_TO_FRONT`); RetroArch with the same ROM and core just comes forward (`onNewIntent`) | **needs launcher** |
| Another game: the frozen one ends first, saving | the launcher asks the old one to finish (each emulator saves on `onDestroy`/its own quit; Android can kill a stopped app at any time, which the Linux freezer prevented) | **needs launcher**; save-on-kill **unverified** per emulator |
| Held 3 s: quick menu on the bottom screen over a running game | the launcher draws an overlay window on the bottom display (`TYPE_APPLICATION_OVERLAY`, needs `SYSTEM_ALERT_WINDOW`, a privileged/pre-granted permission) with brightness, profile, fan, lights from `banana-hwd` | **needs launcher**; that the top-screen game keeps running and focused: **unverified** |
| The pad's own Home key (`KEY_HOME`) | Android's HOME already (hardware/ayn's keylayout): brings the launcher forward on its own | as Android |

## Quick menu tiles (THOR-OS.md, `sysstat`)

CPU/GPU load, clocks, temperatures, memory, battery power, fan: `banana-hwd`'s `STATS` with sysstat's keys
(**done here**). Under SELinux, the GPU's kgsl nodes are vendor-labelled and not granted until their type is read on a
Thor (`sepolicy/vendor/banana_hwd.te`): `gpu`/`gpu_mhz` missing until then (**needs ROM**, one line each). Battery and
memory also come from Android's own APIs (`PROTOCOL.md`).

## Sleep (THOR-OS.md, `bananad`, `handheld sleep`)

| Linux | Android | State |
|---|---|---|
| bananad's power key, lid, idle dim and sleep, suspend with fallbacks | Android's power manager; the lid is `SW_LID` with `config_lidControlsSleep=true` (device overlay); idle timeout in the ROM's settings defaults | **needs ROM** (exists in LineageOS) |
| Lights off for a sleep (lit, they keep the SoC awake) | the launcher sends `SUSPEND`/`RESUME` on screen off/on | **done here** (daemon); **needs launcher** (the broadcast receiver) |
| Fan stopped for a sleep | the kernel's thermal governor keeps the fan on this kernel | as the kernel does |
| Power off after N minutes asleep | none in LineageOS | **not possible** without a ROM change (an alarm in the launcher could do it: **needs launcher**) |
| A game held over a sleep, continued after | Android keeps the paused app | as Android |

## Fan (THOR-OS.md, `handheld fan`)

Linux: ROCKNIX's curve on `pwm1` in a loop (mainline's device tree maps no zone to the fan). AYN's kernel, which
LineageOS uses, **does** map it: `pwm-fan` is a cooling device that `step_wise` drives from several zones, and a
write to `pwm1` is undone at the next evaluation. So `banana-hwd` moves the trip points bound to the fan, the method
and safety rules of thor-features' `fanctl` (Apache-2.0): profiles `stock`, `quiet` (the default: only `usb-therm`'s
43 °C trip, which spun the fan at idle while charging, moved to 60 °C), `moderate` (-5 °C), `aggressive` (-10 °C),
`max` (from 40 °C). Never above a boot value, never a trip removed. No "off" and no fixed speed on this kernel: those
were Linux-only (the kernel's protection stays).

**done here** (tested against a stand-in `/sys` built from fanctl's description); on a Thor: **unverified**.

## Stick lights (THOR-OS.md, `handheld leds`)

Off, battery colour, the system's colour, one colour, three levels, breathing: all in `banana-hwd` (`LEDS ...`), on
AYN's nodes `left:stick:0`-`3` and `right:stick:0`-`3` (`multi_intensity` "R G B", from LineageOS's hardware/ayn), with
SELinux through the attribute hardware/ayn exports for its own AynParts. **done here**; on a Thor **unverified**.

Conflict: LineageOS's **AynParts** (Settings > AYN, RGB lighting) writes the same nodes when its switch is on, and
at boot. **needs ROM**: leave its RGB switch off (its default) or take AynParts out (it also holds the stick
calibration and button mapping pages, which are worth keeping). LineageOS's `charger_rgb` lights them in off-mode
charging only, which doesn't conflict.

## Performance modes (THOR-OS.md, `banana-perf`)

Per-system and per-game profiles (`ui`, `powersave`, `balanced`, `steady`, `performance`, `superdrastic`), each a share
of each cluster's top clock: `PERF SET` and `GAME START <system> <profile>` in `banana-hwd`. AYN's kernel has
`walt`, not `schedutil`, so a cluster keeps `walt` where the Linux image set `schedutil` (the caps still apply).
**done here**. Qualcomm's power HAL also moves these limits for its boosts; whether it undoes the caps during a game:
**unverified** (if it does: **needs ROM**, its `perfconfigstore.xml`/boost hints). The GPU boost row: offered only if
the kernel's table has the higher clock (AYN's kgsl table not read here): **unverified**.

## Dual screens (THOR-OS.md "How it is put together")

| Linux | Android | State |
|---|---|---|
| the menu on both panels through KMS | the launcher as HOME on the top display and SECONDARY_HOME on the bottom one (ROM overlay) | **needs launcher** / ROM agent's overlay |
| RetroArch on one panel, the menu's game panel (save/load/companions) on the other | RetroArch on the top display; the launcher's game panel on the bottom display while the game runs (two apps resumed at once: Android's multi-resume) | **needs launcher**; RetroArch's commands from our panel (save/load state) need RetroArch's network command interface (UDP 55355, `network_cmd_enable` in the config we pass): **unverified** |
| a "second screen" window (melonDS, Azahar) to the bottom panel | each app's own handling of a second display (Azahar and melonDS have dual-screen options; none was tried on two displays) | **unverified** |
| a PC game's bottom screen switched off | the launcher can dim the bottom display (`BACKLIGHT bottom 0`, or Android's per-display brightness) | **needs launcher** |
| both panels' brightness separately | `banana-hwd BACKLIGHT top|bottom` (raw), and Android's own per-display brightness | **done here** (raw) / **needs launcher** |

## Web browser (THOR-OS.md "The web browser")

WPE WebKit and an on-screen keyboard. LineageOS's browser (Jelly) is removed by the ROM; Android System WebView stays.
`launch.conf` `[browser]` is `banana:browser`, a page of the launcher on WebView, with Android's own keyboard on the
bottom display (the bottom display has `shouldShowIme`). **needs launcher**.

## Home server (RomM) sync (THOR-OS.md, `banana-sync`)

Games, box art and saves from a RomM server, paired by code. A Linux program; on Android the same HTTP calls from
the launcher, into the card's folders. Saves need each emulator's save folder: RetroArch's are on shared storage
(its config), the standalone emulators' are in their own folders, several in app-private storage (`Android/data/<pkg>`,
which other apps can't write since Android 11; the launcher, privileged, still can't without `MANAGE_EXTERNAL_STORAGE`).
**needs launcher**; save sync for app-private folders: **not possible** for some emulators (each app's own sync or
export instead).

## Bshare (LocalSend) and Files, file sharing, SSH

| Linux | Android | State |
|---|---|---|
| Bshare: LocalSend to and from phones | the launcher's page speaking LocalSend's protocol, or the LocalSend app (MIT, F-Droid `org.localsend.localsend_app`) | **needs launcher** (or owner installs LocalSend) |
| Files: the cards' folders, copy/move/rename/delete | the launcher's page with all-files access (`MANAGE_EXTERNAL_STORAGE`, pre-granted by the ROM) | **needs launcher**, **needs ROM** (the grant) |
| Files' servers (SMB, SFTP, FTP), ksmbd, dropbear | none in LineageOS; an app could serve SFTP/FTP from userspace | **not possible** as it was (no kernel SMB server); **needs launcher** for a userspace one |
| SSH | `adb shell` | see USB debugging |

## Store, Activity, PortMaster, Döda Kvarter

Linux programs (DRM/evdev, glibc, SDL on KMS). `launch.conf` marks them `package = none`: hidden on Android.
**not possible**. Activity's play time can be shown by the launcher from its own records (**needs launcher**).

## Updates / OTA (THOR-OS.md "Updates")

Linux: A/B slots from GitHub releases, applied from the menu. Android: A/B OTA zips from the ROM's build; LineageOS's
Updater app is removed by the ROM (it would offer LineageOS's builds). Applying a BananaOS OTA from the menu needs
`update_engine` access (system permission) and a server: **needs ROM** (a build server and an updater) and
**needs launcher**. Emulator updates: each app from its own channel (they keep their signatures).

## USB debugging (THOR-OS.md "USB debugging")

| Linux | Android | State |
|---|---|---|
| on by default, no password; network card (ssh), serial console, adb | adb over USB, on at boot: `persist.sys.usb.config=mtp,adb` (`props/banana.prop`; the ROM's `banana.mk` sets it already) | **done here** |
| no password | `ro.adb.secure=0`: build with `WITH_ADB_INSECURE=true` (LineageOS's switch). The ROM keeps authorization by default (the computer's key accepted once on screen). `banana.prop` explains the trade-off: anyone with a cable gets a shell, root on userdebug | **needs ROM** (a build-time choice) |
| network card over USB, ssh | `adb forward`/`adb shell`; RNDIS tethering exists in Android's USB menu | as Android |
| serial root shell | none | **not possible** |

## Themes and setup

| Linux | Android | State |
|---|---|---|
| themes (Thor OLED default, the others selectable) | the launcher's own rendering of the same themes | **needs launcher** |
| first-time setup: language, Wi-Fi, time zone, name, RetroAchievements, home server, Steam, fan and lights, box art | the launcher's setup, using Android's settings intents for Wi-Fi/time zone/language (no setup wizard: the ROM removes it), `banana-hwd` for fan and lights, GameNative's own screen for Steam | **needs launcher**; Steam step: in GameNative |
| permissions an emulator asks for at its first start (all-files access) | **needs ROM**: pre-grant at first boot (`MANAGE_EXTERNAL_STORAGE` app-op for RetroArch, Azahar, Vita3K and the launcher; runtime permissions through `default-permissions` XML), else each emulator asks once | **needs ROM** / **needs launcher** (a privileged launcher with `MANAGE_APP_OPS_MODES` can set the app-op itself) |

## Sound, Wi-Fi, Bluetooth, memory, tuning

| Linux | Android | State |
|---|---|---|
| PipeWire over AudioReach | Qualcomm's audio HAL | as LineageOS (**needs ROM**: nothing) |
| wpa_supplicant, ath12k; BlueZ | qcacld ("kiwi"), Qualcomm's BT stack | as LineageOS; 6 GHz WPA3 known issue (`docs/THOR-LINEAGE.md`) |
| zram at half the RAM, LZ4 + zstd recompression | no zram in the device's fstab | **needs ROM** (one fstab line, `docs/THOR-LINEAGE.md`) |
| `tune`: sysctls, MGLRU TTL, THP defer, cpu0 idle quirk, card I/O scheduler, OOM protection | LineageOS/vendor init scripts set their own; none of the Linux values applied | **needs ROM** (init `write`s in the device's .rc, each to be measured as THOR-TUNING.md says); the cpu0 idle quirk is mainline-specific (GMU), not for the vendor kernel |
| Mesa Turnip with autotune patches | Qualcomm's Adreno driver (blobs); emulators that bundle Turnip (Dolphin, Azahar, ARMSX2, Vita3K with custom drivers) can load their own | as each app |

## What was checked here, and what wasn't

Checked on a PC:

- every APK in `fetch.sh` downloaded from its URL, hashed (ScummVM's matched its published `.sha256`), its binary
  manifest parsed for package, version, minSdk/targetSdk and ABIs, then deleted; `fetch.sh` itself ran once (Dolphin);
- every activity, action and extra in `launch.conf` read in the emulator's source at the pinned version (files named
  in each section), and compared with ES-DE's Android configuration; `launch.conf` parses as INI and covers every id
  of `systems.conf`;
- RetroArch's Android cores: every core `launch.conf` names was listed on buildbot's arm64-v8a nightly;
- DepotDownloader 3.4.0 `linux-arm64`: its ELF interpreter and libraries (glibc), so it can't run on bionic;
- `banana-hwd` and `banana-hwc`: built with GCC and Clang (`-Wall -Wextra -Werror`) for the host and cross-compiled
  for aarch64 (glibc); `tests/hwd-test.sh` (82 checks) against two made-up kernels' `/sys` (AYN's vendor kernel and
  mainline), with AddressSanitizer and UBSan clean; the Home button through a pipe standing in for the input device.

Not checked:

- nothing was built in a LineageOS tree: `Android.bp` (both), the `.rc`, the SELinux policy (in particular the
  `dac_override_allowed` attribute, `proc_random`, `vendor_sysfs_ayn_leds_type` and the vendor file's
  `vendor_sysfs_battery_supply` against the tree's neverallows) and the bionic build of the daemon;
- nothing ran on a Thor: the trip-point profiles (rules from fanctl's measurements, not ours), the lights' node names
  and channel order (from LineageOS's hardware/ayn sources), the AYN key's device name, the backlights, the kgsl and
  power-supply labels;
- no emulator was started by intent on Android; ScummVM's core with a `.scummvm` folder, Flycast with a foreign
  content URI, melonDS with a ROM outside its library, GameNative with a game not yet downloaded;
- `mamemess` carrying the Game & Watch drivers.

## Sources

- AYN's LineageOS: [android_hardware_ayn](https://github.com/LineageOS/android_hardware_ayn) at `73eedc7` (AynParts'
  `LightUtils.kt`, `services/charger_rgb/main.cpp`, `init/ueventd.ayn.rc`, `sepolicy/qti/`,
  `configs/keylayout/Vendor_045e_Product_028e_Version_1234.kl`);
  [android_device_ayn_qcs8550-common](https://github.com/LineageOS/android_device_ayn_qcs8550-common) (`init/init.qcom.rc`:
  the backlights' chown; `init/init.target.rc`)
- [Wingdom/thor-features](https://github.com/Wingdom/thor-features) (Apache-2.0): `thor-fanctl` (trip points, usb-therm,
  settle), `thor-aynkey` (KEY_F24 on gpio-keys), `thor-screenblank` (panel0 top, panel1 bottom), `thor-stickfx`,
  `docs/ayn-button-panel.md` (one hwmon, no fan RPM)
- the emulators' sources at the pinned versions (listed in `launch.conf`), and ES-DE's
  `resources/systems/android/es_systems.xml` and `es_find_rules.xml`
- BananaOS's Linux tools: `system/rootfs/usr/libexec/banana/handheld`, `sysstat`, `steam`, `usr/bin/banana-perf`,
  `etc/banana/hw/thor.conf`

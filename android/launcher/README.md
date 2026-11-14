# BananaLauncher: banana-ui as Android's HOME app

BananaOS's menu (`ui/`, the same C++ core as on the Buildroot image) built as an Android app, the launcher of the
LineageOS-based BananaOS for the AYN Thor (`docs/THOR-LINEAGE.md`). Same look, same pages, same settings file on the
card; games start in Android emulators through intents.

Package `org.bananaos.launcher`; HOME activity `org.bananaos.launcher/.HomeActivity` (display 0, the top screen);
SECONDARY_HOME activity `org.bananaos.launcher/.BottomActivity` (display 1, the bottom screen).

**State: built and checked on a PC, never run on a Thor or any Android device.** What was checked is at the end.

## How it is put together

```
 HomeActivity (display 0)     BottomActivity (display 1)     QuickSheet (overlay, display 1)
   ScreenView: surface 0         ScreenView: surface 1           SurfaceView: surface 2
        |   keys, sticks, touches   |                                  |
        +-------------+-------------+----------------------------------+
                      | JNI (Core.java  <->  ui/src/android_jni.cpp)
            libbanana.so: ONE core, its main() on a thread of its own
              main.cpp (unchanged but for 4 #ifdef __ANDROID__ blocks)
              SoftBackend renders both screens as on Linux
              android_display.cpp  Display (kms.h) on the ANativeWindows: damaged rows copied in, fitted per display
              android_input.cpp    Input (input.h) fed from a queue: the kernel's key codes, stick, touch
              android_tools.cpp    the Linux paths and shell tools, taken where they leave the core (ld --wrap)
              android_platform.cpp the Platform interface: Java (Platform.java) and banana-hwd
              android_launch.cpp   the launch table (android/system/launch.conf) -> an intent spec
```

- **One core, two surfaces.** The core is started once per process by the Application and runs its own main loop
  (poll on an eventfd for input, the surfaces' changes and its workers), exactly as on Linux. Each activity hands it
  its surface; it draws whichever surfaces exist, and draws a surface whole when it comes back (the `compositor()`
  path of `kms_sdl.cpp`). The bottom screen is a second *activity* on display 1 rather than a `Presentation`: the
  system starts a SECONDARY_HOME activity there by itself, it stays up while a game runs on the top screen, and it
  doesn't depend on the top activity's lifecycle (a `Presentation` goes away with its activity when a game comes in
  front). HomeActivity also starts it explicitly (`setLaunchDisplayId`) in case display 1 shows no system decorations.
- **Drawing.** `present()` locks each window on the damaged rectangle (`ANativeWindow_lock`), runs the panel fit of
  `panelfit.h` (the 4:3 menu staged in 1920x1080 and 1240x1080 as on the Buildroot image), swaps red and blue
  (RGBX_8888) and posts. Pacing is the BufferQueue's: the next lock waits for a free buffer.
- **Input.** Keys become the kernel's codes (a pad button keeps its scan code, so the buttons mean what they mean on
  BananaOS's image and `BANANA_CONFIRM` works as there); the left stick and hat go through `stickStep()`; touches are
  fractions of the touched surface, mapped by `touchToCanvas` as the evdev ones are.
- **Platform.** The core's ~45 calls to `/usr/libexec/banana/*` (popen, system, fork+exec) and its few sysfs and FHS
  paths stay as they are. They are taken where they leave the core, with ld's `--wrap` (CMakeLists.txt's `WRAP`), so
  no shared source changed for them and the Linux build can't be affected:
  - paths: `/etc/banana`, `/usr/share/banana`, `/run/banana`, `/sys/class/{backlight,power_supply,net/wlan0}` and
    `/proc/device-tree/model` are mapped into `<files>/root`, which the app fills from the APK's assets and keeps
    the sysfs mirrors of up to date (battery, Wi-Fi's link, brightness). A brightness written there is applied.
  - tools: a command line naming a shell tool is read (quotes, `;`, `&&`, `||`, `&`, redirections) and run on the
    `Platform` interface (`android_platform.h`). A child the core forked to exec one sends it to the app's process
    over a socket (with its stdout) and waits for the exit status, since a forked child of the VM can't call Java.
  This was chosen over an interface edited into each call site: the tools' command lines already are the core's
  interface (the Linux tests swap them with stand-ins), and the alternative was ~45 edits across 8 shared files.
- **Games.** `runGame()` asks `android::runGame()`: the launch table's entry for the system (its variant if
  `settings.conf` has `launch.<id> = <variant>`), the placeholders filled in (`%ROM%`, `%CORE%`, `%ROMTEXT%`,
  `%ROMKEY:<k>%`; `%ROMURI%` by Java, from `RomProvider`), and Games.java starts the intent on display 0. The menu
  stays where it was behind the game. Tables read, later ones replacing a section whole: the APK's copy of
  `android/system/launch.conf`, `/vendor/etc/banana/launch.conf`, `/system_ext/etc/banana/launch.conf`,
  `banana/launch.conf` on the card. Systems whose entry is `package = none` are left off the shelves (a filtered
  copy of systems.conf); `package = banana:<page>` opens the menu's own page (`banana:browser`: Android's browser).
- **Files.** Games in `roms/` on a removable card that has one (`/storage/<uuid>/roms`), else `/sdcard/roms`;
  `banana/` beside it holds settings.conf and the rest, as on BananaOS's card. Fonts, pictures, languages, themes,
  the Thor's systems.conf and launch.conf are the APK's assets under `root/`, unpacked to `<files>/root` at the first
  start after each install or update.

## The Home button

The button under the bottom screen is a GPIO key sending `KEY_F24` under LineageOS's kernel. It reaches the focused
app only, so while a game has the focus the launcher hears it from **banana-hwd** (`android/system/hwd/PROTOCOL.md`):
the daemon reads the key's input device without grabbing it, times the hold and sends `EVENT home down|tap|hold|
release` to the launcher's subscribed connection (Hwd.java). While the launcher (or its quick sheet) has the focus
the same key also arrives as a KeyEvent (scan code 194); HomeButton.java takes both and drops the second copy of a
press. The pad's `BTN_MODE` is taken as the Home button too when it reaches the launcher.

- **Tap over a game**: the launcher comes forward (`Games.toLauncher`), the game keeps running behind it as an
  Android app in the background (what the emulator does then is its own: most pause in `onPause`). This is the
  Android form of "the menu over a frozen game".
- **Tap in the menu with a game behind it**, or picking the same game again: back to the game's task
  (`ActivityManager.moveTaskToFront`, found with `getRunningTasks`, which REAL_GET_TASKS lets the launcher see; else
  the package's launch intent, which brings its existing task forward).
- **Android's own HOME** (the pad's `KEY_HOME`) does the same through the HOME intent: over a game it brings the
  launcher forward; when the launcher is already in front (paused for the intent, never stopped) it goes back to the
  game.
- **Held three seconds over a game**: the quick menu. The app tells the core (`nativeHome(2)`), and the core's own
  `homeMenu(quick)` runs as on the Buildroot image, with the sheet as an overlay window on display 1
  (`TYPE_APPLICATION_OVERLAY`, QuickSheet.java) instead of a KMS plane: `Display::sheetOpen()` shows it, `sheetAt()`
  draws the bottom screen into it slid by the existing `sheet.h` curves (the rows above its top edge transparent, so
  the game's bottom screen shows through), `sheetClose()` removes it. The game is never paused and the top screen is
  never touched. The overlay has the focus while it is up, so the pad and the bottom touchscreen are the menu's. B,
  or the button again, slides it down.
- A hold in the menu itself does nothing, as on BananaOS.

## What the ROM must provide

- **The platform signature** (`certificate: "platform"` in Android.bp) and **privileged** install on `/system_ext`.
  The signature grants SYSTEM_ALERT_WINDOW (the quick sheet's overlay), MANAGE_EXTERNAL_STORAGE (the card),
  WRITE_SETTINGS, CONTROL_DISPLAY_BRIGHTNESS and NETWORK_SETTINGS. The privileged permissions it uses are allowed by
  name in `privapp-permissions-org.bananaos.launcher.xml` (REAL_GET_TASKS, START_ACTIVITIES_FROM_BACKGROUND,
  CONTROL_DISPLAY_BRIGHTNESS, NETWORK_SETTINGS, BLUETOOTH_PRIVILEGED, SET_TIME_ZONE); its runtime ones (location for
  Wi-Fi's scan, Bluetooth's) are granted at the first boot by `default-permissions-org.bananaos.launcher.xml`.
  A platform-signed privileged app runs in the `platform_app` SELinux domain, which banana-hwd's policy lets connect
  (`android/system/sepolicy/private/banana_hwd.te`).
- **banana-hwd** running with its socket at `/dev/socket/banana-hwd` (`android/system/hwd`): the Home button's
  events, the fan, the stick lights, the performance modes (`PERF SET`, `GAME START <system> <perf>` at each game),
  the stats of the quick menu, and the backlights where Android's own brightness doesn't reach the bottom panel.
- **The key**: nothing to change: `KEY_F24` stays unmapped (no app binds it) and banana-hwd reports it. The stock
  launchers are replaced (Android.bp's `overrides`), and there is no recents screen.
- **The bottom display as a home display**: `config_secondaryHomePackage` = `org.bananaos.launcher` (the ROM's
  `android/product/overlay`; the activity it resolves is `.BottomActivity`, the package's SECONDARY_HOME), and
  display 1 allowed to show system decorations so the system starts it there. HomeActivity starts it anyway.
- **`launch.conf`** in `/system_ext/etc/banana/launch.conf` (it wins over the APK's copy) and the emulators it names.

## Building the APK

Needs a JDK 17+, Gradle 8.13+, the Android SDK (platform 35, build-tools 35) and NDK r28 (28.2.13676358), and CMake
3.31.6 from the SDK. FreeType is downloaded by CMake (or given with `-PfreetypeDir=<its unpacked source>`).

```sh
sdkmanager "platforms;android-35" "build-tools;35.0.0" "ndk;28.2.13676358" "cmake;3.31.6"
cd android/launcher
ANDROID_HOME=<sdk> gradle assembleRelease      # build/outputs/apk/release/BananaLauncher-release-unsigned.apk
ANDROID_HOME=<sdk> gradle assembleDebug        # a debug-signed one to sideload: adb install -r ...
```

Into the ROM: copy the release APK (unsigned: the build signs it with the platform key) to
`android/launcher/prebuilt/BananaLauncher.apk` (the module `BananaLauncher` of Android.bp, which
`android/product/banana.mk` adds) and build the ROM.

Sideloaded on an ordinary Android device it starts too (it asks for all-files access once); the quick sheet, the
task switching and Wi-Fi's joining then lack their permissions.

Tests on a PC (the launch table against the real `android/system/launch.conf` and the Thor's systems, and the tools'
layer: the shell syntax, the forked children's tools, the mapped paths):

```sh
sh android/launcher/tests/run.sh
```

## Stubbed (TODO)

What the Buildroot image does that the Android build doesn't yet (each logged once as `TODO(android)` when asked):

- Tools answered "not here" (exit 127): `update` (OTA: the ROM's own updater instead), `steam` and `pc-games` (PC
  games go to GameNative / Winlator through launch.conf; the Steam login and downloads pages are hidden),
  `rocknix-options`, `nds-reset`, `ra-reset`, `ssh`, `share` (file sharing), `sd2`, `boot-log`, `saves-flat`,
  `data-layout`, `data-link`, `tune`, `panel-tune`, `one-panel`, `usb-debug`, `sound-server`.
- Wi-Fi: scan, status, on/off, the setup's test and joining the network chosen in the menu (privileged
  `addNetwork`) work; WPA3-only and enterprise networks, and a join the system refuses, open the system's Wi-Fi panel.
- Bluetooth: on/off and the paired devices' list work; search, pairing, connecting and forgetting open the system's
  Bluetooth page.
- Time zone: settings.conf keeps a POSIX rule; only an Olson name (`Europe/Paris`) is passed to the system. The
  menu's own clock follows the setting either way.
- Screen colours (the DRM gamma table): not applied.
- The menu's sounds (ALSA on Linux): silent; an AAudio backend for `sound.cpp` is to do.
- The game panel beside a RetroArch game (the leased panel, the companions, the touch tiles, the DS companion's
  overlay, the virtual keyboard): not on Android. The bottom screen shows the menu's bottom screen while a game runs.
- Files' servers (`banana-fetch`), Bshare (`banana-bshare`), home server sync (`banana-sync`), box art scraping
  (`banana-scrape`): their helper programs aren't built for Android.
- A game's end isn't seen: its play time is recorded when the launcher first comes back over it.
- Performance: the core's own clock boost while scrolling (`banana-perf ui`) maps to `PERF SET ui`; the CPU stays the
  framework's otherwise.
- A Settings row for `launch.<id>` (the emulator variant): the key works, the row isn't there yet.

## What was checked, and what wasn't

Checked on a PC: the APK builds (debug and release, arm64-v8a, `aapt2 dump badging` reads it, 16 KB page-aligned
native library); the core links with every Linux call taken by the wrappers (`llvm-objdump` shows `quickPoll` calling
`__wrap_popen`); `tests/run.sh` passes (the real launch.conf reads with no warning and every system it leaves on the
shelves has an entry); the Linux builds (`make -C ui host`, the aarch64 cross build) and `ui/tests/run.sh` are as
before.

Not checked: anything on a device. First things to look at on a Thor: that both surfaces come up at the panels'
sizes (logcat tag `banana-ui`), that display 1's activity starts, the overlay over a game and its focus, the Home
button's events from banana-hwd, `moveTaskToFront` back into each emulator, and each launch.conf entry.

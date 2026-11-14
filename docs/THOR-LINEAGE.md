# BananaOS on LineageOS: the AYN Thor

A third route to the AYN Thor (Snapdragon 8 Gen 2 class, Base, Pro and Max; not the Thor Lite, which is an SM8250
handheld). BananaOS's menu runs as an Android launcher on a stripped LineageOS. The hardware layer is LineageOS's
official Thor support. The other routes are BananaOS's own Buildroot image ([`THOR-OS.md`](THOR-OS.md)) and
ROCKNIX with Banana's menu ([`THOR-ROCKNIX.md`](THOR-ROCKNIX.md)).

**Status: written, not built, not booted.** Nothing on this page has run on a Thor. Nothing here has been built
either: a LineageOS tree needs about 300 GB and a strong PC. What *was* checked is listed under
[What was checked](#what-was-checked). Everything else is marked as unproven.

## Contents

- [What exists already](#what-exists-already)
- [How it is put together](#how-it-is-put-together)
- [Host requirements](#host-requirements)
- [Building](#building)
- [Flashing](#flashing)
- [Memory](#memory)
- [The Thor's hardware under Android](#the-thors-hardware-under-android)
- [What was checked](#what-was-checked)
- [Unproven](#unproven)
- [Risks](#risks)
- [Moving the pins](#moving-the-pins)
- [Sources](#sources)

## What exists already

The Thor is an **official LineageOS device**. The codename is `odin2thor` and the maintainers are AnierinBliss and
npjohnson. LineageOS's build targets list `odin2thor userdebug lineage-24.0 W`, so LineageOS builds it weekly. So
BananaOS doesn't need a device tree of its own. `android/device/` is not used.

| What | Where | Branch / last activity (checked 2026-11-14) |
|---|---|---|
| LineageOS 24.0 = **Android 17** (AOSP tag `android-17.0.0_r1` in LineageOS's `default.xml`; released 2026-10-11/12) | [LineageOS/android](https://github.com/LineageOS/android) | `lineage-24.0` |
| Thor device tree | [LineageOS/android_device_ayn_odin2thor](https://github.com/LineageOS/android_device_ayn_odin2thor) | `lineage-24.0` @ `239aa70` (2026-10-29); also `lineage-23.2` |
| Common tree for the SM8550 AYNs | [LineageOS/android_device_ayn_qcs8550-common](https://github.com/LineageOS/android_device_ayn_qcs8550-common) | `de86751` (2026-10-31) |
| AYN hardware (AynParts, charger/recovery RGB, ueventd) | [LineageOS/android_hardware_ayn](https://github.com/LineageOS/android_hardware_ayn) | `73eedc7` (2026-11-12) |
| Kernel (Qualcomm 5.15 GKI for kalama, AYN "moorechip" config) | [android_kernel_ayn_qcs8550](https://github.com/LineageOS/android_kernel_ayn_qcs8550), [-devicetrees](https://github.com/LineageOS/android_kernel_ayn_qcs8550-devicetrees), [-modules](https://github.com/LineageOS/android_kernel_ayn_qcs8550-modules), [common-modules](https://github.com/LineageOS/android_kernel_ayn_common-modules) | `5646ff2`, `9b2d195`, `efe02f6`, `8a706e5` |
| Proprietary files (blobs and firmware images) | [TheMuppets/proprietary_vendor_ayn_odin2thor](https://github.com/TheMuppets/proprietary_vendor_ayn_odin2thor), [..._qcs8550-common](https://github.com/TheMuppets/proprietary_vendor_ayn_qcs8550-common) | `556875d` (2026-07-05), `00fa869` (2026-10-27); from AYN's stock build `Thor.20260112.215904` |
| Siblings on the same common tree | [odin2](https://github.com/LineageOS/android_device_ayn_odin2), [odin2mini](https://github.com/LineageOS/android_device_ayn_odin2mini), [odin2portal](https://github.com/LineageOS/android_device_ayn_odin2portal) | `lineage-24.0` (Oct 2026); separate `*_ack` trees exist for a newer-kernel variant (`android_device_ayn_qcs8550-ack`, `android_kernel_ayn_kernel-ack`) |
| Community extras for LineageOS 24 on the Thor: fan profiles, AYN button actions, stick lighting, separate brightness per screen, a 6 GHz Wi-Fi firmware fix, exFAT fsck for large cards (Magisk modules) | [Wingdom/thor-features](https://github.com/Wingdom/thor-features) | 2026-11-11 |

Per LineageOS's device data ([`lineage_wiki/_data/devices/odin2thor.yml`](https://github.com/LineageOS/lineage_wiki)):

- **Hardware.** SoC "Qualcomm Dragonwing QCS8550", network "None" (no modem), RAM 8/12/16 GB, UFS 3.1. Top panel 6"
  1920x1080 OLED at 120 Hz, bottom panel 3.92" 1240x1080 OLED at 60 Hz.
- **Peripherals.** No camera is listed, and the common tree sets `PRODUCT_NO_CAMERA := true`.
- **Firmware.** The stock firmware must be **Android 13** (`before_install: needs_specific_android_fw, version 13`),
  and LineageOS's builds carry the firmware themselves (`ships_fw: true`).
- **Install.** The install method is `fastboot_unlocked`. Before recovery, the "boot stack" goes on: boot, dtbo,
  vbmeta and vendor_boot. Then the recovery.
- **Kernel.** 5.15.

The stock Android version is 13, from that data and from `BOARD_SHIPPING_API_LEVEL := 33`.

**AYN's own sources.** AYN publishes mainline Linux work for the SM8550 handhelds:

- [AYNTechnologies/linux](https://github.com/AYNTechnologies/linux), branches `sm8550/v6.13.4`, `sm8550/v6.17.5` and
  `ayn/v7.0`
- `u-boot`, `alsa-ucm-conf` and `audioreach-topology`

These are for Linux images, not Android. A GPL release of the Thor's *stock Android* kernel by AYN wasn't found. The
kernel LineageOS uses is Qualcomm's CLO kernel with AYN's drivers (`ayn-drivers`, moorechip device trees). AYN's stock
OTA or full-firmware downloads were not found either: no official link turned up. TheMuppets' blobs come from the
stock build named above.

## How it is put together

```
LineageOS 24.0 tree (repo)
├── device/ayn/odin2thor, device/ayn/qcs8550-common, hardware/ayn, kernel/ayn/*   LineageOS's Thor support
├── vendor/ayn/odin2thor, vendor/ayn/qcs8550-common                              TheMuppets' blobs + firmware
└── vendor/banana  = this repository (android/manifest/banana.xml)
    ├── android/product/      the product: lineage_banana_thor (this page's part)
    ├── android/launcher/     BananaLauncher, the menu as an Android app
    └── android/system/       banana-hwd + its .rc and sepolicy, the emulator APKs, the launch table, PC games
```

| File | What it does |
|---|---|
| `android/manifest/banana.xml` | `.repo/local_manifests/` file: the Thor's trees, pinned, and this repository at `vendor/banana` |
| `android/product/AndroidProducts.mk` | declares `lineage_banana_thor` (`breakfast banana_thor`) |
| `android/product/lineage_banana_thor.mk` | LineageOS's own Thor product, but on LineageOS's *mini* wifi-only tablet config, plus `banana.mk` |
| `android/product/banana.mk` | adds BananaLauncher, banana-hwd, banana_emulators, the overlays, the strip modules; properties |
| `android/product/strip/Android.mk` | `BananaStrip` and `BananaStripAggressive`: modules that install nothing and *override* what is removed, each removal with its reason |
| `android/product/overlay/` | resource overlays: the bottom panel's HOME, no screensavers, no lock screen, half-length animations, short quick settings |
| `android/product/BoardConfigBanana.mk` | board-level: picks up `android/system/sepolicy` in whichever layout it has. Reached through LineageOS's `vendor/extra/BoardConfigExtra.mk` hook (a linkfile in the manifest) |

**What runs where, against the Buildroot image:**

| | Buildroot image (`THOR-OS.md`) | LineageOS route |
|---|---|---|
| Bootloader | OpenABL on `abl_a/b`, the system on the SD card | AYN's ABL (stock, re-flashed by every LineageOS install), the system on the internal UFS (A/B, dynamic partitions) |
| Kernel | mainline 7.2 + ROCKNIX's 71 SM8550 patches | Qualcomm 5.15 GKI + vendor modules (LineageOS) |
| GPU | freedreno/turnip (Mesa) | Qualcomm's Adreno driver (blobs) |
| Init and services | busybox init, `rcS`, `bananad`, PipeWire | Android init, system_server, SystemUI, the HALs (audio, display, sensors, power, thermal...) |
| Menu | `banana-ui` on KMS, both panels | `BananaLauncher` as HOME on the top display and SECONDARY_HOME on the bottom one |
| Keys, fan, lights, sleep | `bananad`, `usr/libexec/banana/handheld` | `banana-hwd` (android/system), the kernel's thermal governor for the fan |
| Emulators | Linux builds (RetroArch, standalones under `run-wayland`) | the Android APKs (`banana_emulators`) |
| Updates | A/B squashfs slots from Banana's own update | A/B OTA zips, installed with `adb sideload` or by the recovery |

## Host requirements

These are LineageOS's general build requirements. They have not been measured for this product.

- **OS.** 64-bit Linux, Ubuntu 22.04 or 24.04 LTS, or anything LineageOS's
  [build guide](https://wiki.lineageos.org/devices/odin2thor/build/) covers. macOS and WSL1 don't work.
- **Disk.** About **300 GB** free: the sync is about 150 GB of sources and `.repo`, `out/` is 100-150 GB, and ccache
  is optional (50 GB if used). Use an SSD: on a hard disk the build takes many times longer.
- **RAM.** **32 GB or more**. 16 GB works only with a large swap and `-j` lowered. Soong and the R8/metalava steps
  peak above 16 GB.
- **CPU and time.** 8 or more cores. A first build takes several hours on a modern 8-16 core desktop; later builds
  take minutes to an hour.
- **Packages.** `git`, **`git-lfs`** (TheMuppets' firmware images are LFS objects), `repo`, `python3`, Java comes with
  the tree, and the packages from LineageOS's guide:

```
sudo apt install bc bison build-essential ccache curl flex g++-multilib gcc-multilib git git-lfs gnupg gperf \
  imagemagick lib32readline-dev lib32z1-dev libelf-dev liblz4-tool lz4 libsdl1.2-dev libssl-dev libxml2 \
  libxml2-utils lzop pngcrush rsync schedtool squashfs-tools xsltproc zip zlib1g-dev python3 python-is-python3
git lfs install
```

- `adb` and `fastboot` from Google's platform-tools, recent: 35 or newer.

## Building

```
mkdir -p ~/android/banana && cd ~/android/banana
repo init -u https://github.com/LineageOS/android.git -b lineage-24.0 --git-lfs --no-clone-bundle
mkdir -p .repo/local_manifests
curl -o .repo/local_manifests/banana.xml \
  https://raw.githubusercontent.com/JorreFog/BananaLinux/main/android/manifest/banana.xml
#   (or copy android/manifest/banana.xml from a checkout)
repo sync -c -j8 --force-sync
```

Then check the firmware images came down as real files, not LFS pointers:

```
ls -l vendor/ayn/odin2thor/radio/      # several MB each; 130-byte files mean git-lfs was missing
#   if needed: (cd vendor/ayn/odin2thor && git lfs pull)
```

**Proprietary files.** The manifest already pulls TheMuppets' blobs. There is no extraction step unless you want your
own:

- **From a Thor running LineageOS 24.0, over adb with root:**

  ```
  cd device/ayn/odin2thor && ./extract-files.py
  ```

- **From LineageOS's official `odin2thor` zip:**

  ```
  ./extract-files.py <path to lineage-24.0-*-odin2thor-signed.zip>
  ```

  LineageOS's "extracting blobs from zips" guide covers this. The extract_utils scripts take a zip or a dump
  directory.

- **From AYN's stock firmware.** The files to extract are listed in `device/ayn/odin2thor/proprietary-files.txt`,
  `proprietary-firmware.txt` and `device/ayn/qcs8550-common/proprietary-files.txt`. AYN's stock build is
  `Thor.20260112.215904`. A downloadable stock image was not found, so a dump from a Thor on stock (rooted, `adb pull`
  of `/vendor`, `/odm` and the firmware partitions) would be the way. This hasn't been tried.

**Build:**

```
source build/envsetup.sh
breakfast banana_thor             # lunch lineage_banana_thor-<release>-userdebug; <release> is LineageOS's
                                  # (vendor/lineage/vars/aosp_target_release, cp2a on 2026-11-14)
#   optional, lighter and riskier:  export BANANA_STRIP_AGGRESSIVE=true   (before breakfast)
m bacon -j$(nproc)                # or: mka bacon
```

`breakfast` can't fetch this product's dependencies the way it does for official devices (roomservice only knows
LineageOS's names). That is why the local manifest lists every tree.

**Output** in `out/target/product/odin2thor/`:

- the install zip, `lineage-24.0-<date>-UNOFFICIAL-banana_thor.zip`
- `boot.img`, `dtbo.img`, `vbmeta.img`, `vendor_boot.img` and `recovery.img`

The build **fails at product configuration** until `BananaLauncher`, `banana-hwd` and `banana_emulators` exist.
LineageOS requires every listed package to exist. These modules come from `android/launcher` and `android/system`.

## Flashing

LineageOS's install method for the Thor is `fastboot_unlocked`. That is LineageOS's template for devices whose
bootloader **ships unlocked** (the same as the Odin 2 family). The fact that OpenABL can be flashed to `abl_a`/`abl_b`
with plain `fastboot flash` (`THOR-PORT.md`) agrees. Check it first:

```
fastboot getvar unlocked          # expect "yes"; if "no": fastboot flashing unlock (erases the handheld)
```

1. **Be on stock Android 13** with AYN's latest update, as LineageOS requires. A LineageOS zip carries its own
   firmware images, but the first install expects stock firmware underneath.
2. **Back up anything on the handheld.** The install wipes `/data`. If OpenABL is installed, note that the LineageOS
   zip writes AYN's stock ABL back (see [Risks](#risks)).
3. **Enter fastboot.** With the Thor off, hold **Volume Down + Power**.
4. **Flash the boot stack** from the build output:
   ```
   fastboot flash boot boot.img
   fastboot flash dtbo dtbo.img
   fastboot flash vbmeta vbmeta.img
   fastboot flash vendor_boot vendor_boot.img
   fastboot reboot bootloader
   fastboot flash recovery recovery.img
   ```
   On an A/B device, fastboot writes to the current slot.
5. **Boot the recovery.** From the fastboot menu choose *Recovery Mode*. Then *Factory reset* › *Format data / factory
   reset* (needed the first time).
6. **Install.** *Apply update* › *Apply from ADB*, then on the computer:
   ```
   adb sideload lineage-24.0-<date>-UNOFFICIAL-banana_thor.zip
   ```
   The zip also writes the firmware partitions on both slots: abl, xbl, tz, modem, dsp and the rest.
7. **Reboot.** The first boot takes a few minutes. Provision completes setup silently, and BananaLauncher comes up as
   HOME.

**Updates** are the same zip, sideloaded from the recovery (step 6, no format). LineageOS's Updater app is removed:
it would offer LineageOS's own odin2thor builds.

**Going back to stock** is outside this page. It needs AYN's stock image, which wasn't found (above).

## Memory

The target is the lightest LineageOS that runs the same features. Android can't get near the Buildroot image's
footprint: the framework is the price of running Android's emulator APKs and their drivers.

**Estimate, not a measurement.** These numbers are from general Android 14-16 figures on 8 GB phones, not from a
Thor. Measure with `adb shell cat /proc/meminfo` and `adb shell dumpsys meminfo` at idle on the launcher, after 5
minutes.

| | Buildroot image | LineageOS, stock config | LineageOS, `BananaStrip` | with `BananaStripAggressive` |
|---|---|---|---|---|
| Userspace at idle on the menu | ~150 MB (estimated, `THOR-OS.md`) | ~2.0-2.5 GB used | ~1.4-1.9 GB used | ~1.3-1.7 GB used |
| Biggest pieces | banana-ui, PipeWire | system_server, SystemUI, surfaceflinger (two displays), zygote64, the HALs, the launcher, Trebuchet, cached apps | the same minus Trebuchet, the removed apps' cached processes, dreams | minus the phone process and the wallpaper |

What the Android route does for memory:

- **Fewer packages.** Every removed app is one fewer process Android may start or keep cached (`strip/Android.mk`).
- **One zygote.** The device is already `core_64_bit_only`, so no 32-bit zygote runs.
- **No screensavers.** With no dreams, nothing runs in a lit idle.
- **No lock screen.** Keyguard still exists, it just doesn't show.
- **lmkd.** It kills the heaviest of the lowest-priority processes (`ro.lmk.kill_heaviest_task=true`). The game is
  foreground, so it is the last to go.
- **Never "low RAM".** `ro.config.low_ram=false` is set explicitly: that mode would cripple games.

What was **not** done, and why:

- **zygote's preloaded classes and its GL preload** (`ro.zygote.disable_gl_preload`) are left alone. Preloaded memory
  is shared copy-on-write with every app, and dropping it makes every app (each emulator) load the same classes and
  driver privately, so total use goes up when a game runs and starts get slower.
- **The HALs** are left alone: audio, sensors (the gyro is used by emulators), soundtrigger and the rest. They are
  declared in the vendor's VINTF manifest, and removing one breaks the build's compatibility check or leaves init
  waiting for it. Measure first (`dumpsys meminfo` by process), then drop them in a device-tree fork if one is worth
  it.
- **Swap.** The Thor's LineageOS fstab (`qcs8550-common/init/fstab.default`) has **no zram line**, so there is no swap
  at all. The Buildroot image used zram at half the RAM. Adding it means one line in the device tree's fstab, for
  example `/dev/block/zram0 none swap defaults zramsize=50%,max_comp_streams=8`, which is a fork of qcs8550-common.
  It is left out until measurements say the 8 GB Base needs it.
- **Dalvik heap sizes** stay as the device sets them (`phone-xhdpi-6144-dalvik-heap.mk`). They are per-app ceilings,
  not reservations.

## The Thor's hardware under Android

What LineageOS's trees say about each piece the Buildroot image handled. This is for `banana-hwd` and the launcher.

| Feature | Under LineageOS | Source |
|---|---|---|
| Two screens | top = display `4630946441858561667` (default), bottom = `...482288158084`. Device states 0 `DUAL_SCREEN`, 1 `TOP_SCREEN_ONLY`, 2 `BOTTOM_SCREEN_ONLY`. Bottom has `isHomeSupported`, `shouldShowSystemDecors`, `shouldShowIme`. Both turned with `ro.surface_flinger.*_display_orientation=ORIENTATION_90`. 120 Hz top, games allowed 120 fps | `odin2thor/configs/display/*`, `properties/vendor.prop` |
| Home / AYN button (under the bottom screen) | gpio-keys `custom_key`, gpio41, **`KEY_F24`** (Android `KEYCODE_F24` via Generic.kl), a wakeup key. Different from the Buildroot profile's `HW_HOME_KEY=316` (the pad's BTN_MODE), which the pad also sends (`BUTTON_MODE`), with `KEY_HOME` (102 → `HOME`) | `kernel/ayn/qcs8550-devicetrees/moorechip/kalamap-ayn-odin2-thor.dtsi`; `hardware/ayn/configs/keylayout`; thor-features' `thor-aynkey` |
| Gamepad | the "moorechip" MCU, seen by Android as an Xbox 360 pad (`Vendor_045e_Product_028e`). Sysfs at `/sys/class/moorechip-joystick/joystick/` (calibration, layout, triggers, m0/m1_function, ...), writable by `system` | `hardware/ayn/init/ueventd.ayn.rc`, `AynParts` |
| Stick lights | `/sys/class/leds/left:*`, `right:*` (`brightness`, `multi_intensity`), HTR3212 drivers | `ueventd.ayn.rc`, `leds/kalamap-moorechip-leds-htr3212.dtsi` |
| Fan | `pwm-fan` as a thermal cooling device with cooling-maps (8 levels, step_wise): **the kernel runs it**, unlike the mainline device tree. A daemon changes the curve by moving trip points, as thor-features' `fanctl` does. `/sys/class/hwmon/hwmon*/pwm1` exists. `init.target.rc` still defines `fan_control_turn_on/off` services for a `/vendor/bin/fan_service` that no blob list ships | `thermal/kalamap-moorechip-thermal.dtsi`, `qcs8550-common/init/init.target.rc` |
| Lid | gpio17, `SW_LID`; `config_lidControlsSleep=true` | device dtsi, `Frameworks-Thor-Overlay` |
| Brightness | `/sys/class/backlight/panel0-backlight`, `panel1-backlight`. By default both panels move together; thor-features' `screenbright` separates them | thor-features |
| Sound, Wi-Fi, BT | Qualcomm audio HAL (AudioReach/PAL), qcacld WLAN ("kiwi"), WCN BT | `common.mk` |
| Known LineageOS issues (community) | 6 GHz WPA3 fails (the modem partition's `amss20.bin` is used): fix is `/vendor/firmware/kiwi/amss20.bin` from a newer build. Large exFAT cards can refuse to mount (fsck) | thor-features `thor-wifi-kiwi-fw`, `thor-sdcard-fsck` |

## What was checked

- **Branches and pins.** Every repository in `android/manifest/banana.xml` exists, and its `lineage-24.0` branch
  points at the pinned commit (`git ls-remote`, 2026-11-14). TheMuppets' `muppets.xml` for lineage-24.0 maps the two
  vendor repositories to the same paths.
- **The device trees were read:** `lineage_odin2thor.mk`, `device.mk`, `BoardConfig*.mk`, `common.mk`, the
  dependencies, the overlays, the display configuration and the proprietary lists. The product makefile mirrors
  `lineage_odin2thor.mk` line for line apart from the LineageOS config it inherits.
- **Module names in the strip lists** were taken from the lineage-24.0 makefiles that add them: AOSP's
  `handheld_*.mk`, `full_base.mk`, LineageOS's `common*.mk` and `wifionly.mk`. The override chains were read from
  `packages/apps/Launcher3/Android.bp` and `packages/apps/SetupWizard/app/Android.bp`.
- **Overlay names** exist in lineage-24.0's `frameworks/base` (`core/res`, `SettingsProvider`, `SystemUI`):
  - `config_secondaryHomePackage`, `config_useSystemProvidedLauncherForSecondary`, `config_dreams*`,
    `config_enableWallpaperService`
  - `def_lockscreen_disabled`, `def_window_*_scale`, `def_screen_off_timeout`
  - `quick_settings_tiles_default`
- **The `vendor/extra/BoardConfigExtra.mk` hook** exists in lineage-24.0's `build/make/core/board_config.mk`.

## Unproven

- **No build.** Kati or Soong may reject something nobody has run:
  - the `BUILD_PREBUILT` override modules with `/dev/null` as source. This is a common pattern, untested on 24.0; the
    fallback is the same list as BananaLauncher's `overrides:`
  - the overlays' manifest paths
  - the BoardConfigExtra linkfile
- **No boot.** Several behaviours are expected, not seen:
  - BananaLauncher becoming HOME by being the only one
  - Provision finishing setup
  - the bottom panel getting BananaLauncher's `SECONDARY_HOME`
  - `persist.sys.usb.config=mtp,adb` enabling adb on the first boot
- **The overlay's package name for the bottom panel's HOME** (`org.bananaos.launcher`) is a placeholder until
  `android/launcher` fixes BananaLauncher's package name. It must be changed to match.
- **The idle memory figures** are estimates.
- **The Thor's bootloader shipping unlocked** is inferred from LineageOS's install method and from OpenABL flashing.
  AYN's own statement wasn't found.
- **AYN's stock Android kernel source** was not found, nor stock firmware downloads.
- **Whether the gpio41 key is the button under the bottom screen** comes from thor-features' description, not from a
  Thor in hand.

## Risks

- **OpenABL and dual boot.** TheMuppets' vendor tree puts `abl` (and xbl, tz, ...) in `AB_OTA_PARTITIONS`, so every
  LineageOS zip writes AYN's stock ABL on both slots and **removes OpenABL**. The Buildroot image boots only through
  OpenABL. To keep both, re-flash OpenABL after each install. Whether OpenABL then still boots this LineageOS is
  unknown; its README says Volume Up boots Android.
- **No recents provider.** Removing Launcher3QuickStep leaves `config_recentsComponentName` pointing at a missing
  package. Overview and gesture-navigation swipe-up do nothing, and SystemUI logs the failed bind. Keep 3-button
  navigation, or make BananaLauncher the recents component later.
- **64-bit only.** The device is `core_64_bit_only` (LineageOS's choice; the kernel has `allow_mismatched_32bit_el0`).
  32-bit-only APKs, some older emulators and games, won't install. Changing this costs a second zygote, about
  100-150 MB at idle.
- **Aggressive strip.** Removing TeleService and the telephony providers is untested on a handheld image. It is off
  by default.
- **Overlay precedence.** Banana's overlays are on `/product`, so they take precedence over `/vendor` ones such as
  hardware/ayn's `AynSystemUIResCommon` and the device's overlays. A LineageOS overlay on `/product` setting the same
  value would make the order depend on priority (Banana's are 900). Check with `adb shell cmd overlay list`.
- **Following LineageOS.** The pins freeze a working set; LineageOS moves weekly. A pinned device tree against a newer
  `lineage-24.0` platform can fail to build. See below.
- **Firmware version.** LineageOS requires stock Android 13 firmware underneath for the first install. A Thor updated
  by AYN to a later Android, if that happens, would need checking against LineageOS's wiki first.

## Moving the pins

To follow LineageOS:

1. Set each `revision` in `banana.xml` to its branch (`lineage-24.0`) or a newer commit, then `repo sync`.
2. Diff `device/ayn/odin2thor/lineage_odin2thor.mk` against `android/product/lineage_banana_thor.mk`. The
   device-specific lines must match.
3. Grep the new `vendor/lineage/config/common*.mk` and `build/make/target/product/handheld_*.mk` for packages added
   since, and decide whether they go in `strip/Android.mk`.
4. Check `vendor/lineage/vars/aosp_target_release` if `lunch` complains about the release name.

On a new LineageOS major version, also re-check:

- LineageOS's device data for the firmware requirement
- `proprietary-files.txt` for the stock build the blobs come from

## Sources

- **LineageOS.**
  - [manifest](https://github.com/LineageOS/android) (`lineage-24.0`, `default.xml`: `android-17.0.0_r1`)
  - [build targets](https://github.com/LineageOS/hudson) (`lineage-build-targets`, `updater/devices.json`)
  - [wiki source](https://github.com/LineageOS/lineage_wiki) (`_data/devices/odin2thor.yml`, install templates)
  - [Thor wiki page](https://wiki.lineageos.org/devices/odin2thor/) (403 from here; read through the wiki's source)
- **LineageOS 24.0 release coverage.**
  - [9to5Google](https://9to5google.com/2026/10/12/lineageos-24-launches-based-on-android-17-with-app-redesigns-and-simple-flash-tool/)
  - [Android Authority](https://androidauthority.com/lineageos-24-android-17-3721683)
  - [Android Authority on AYN devices](https://www.androidauthority.com/ayn-odin-2-3-thor-lineageos-3673805/)
- **Device, kernel and hardware trees.** The links in the table at the top. Blobs:
  [TheMuppets manifests](https://github.com/TheMuppets/manifests) (`lineage-24.0/muppets.xml`).
- **Community.** [Wingdom/thor-features](https://github.com/Wingdom/thor-features).
- **AYN.** [AYNTechnologies/linux](https://github.com/AYNTechnologies/linux) (mainline Linux branches),
  [AYNTechnologies/u-boot](https://github.com/AYNTechnologies/u-boot).
- **This repository.** `docs/THOR-PORT.md`, `buildroot/board/thor/openabl-README.txt`,
  `system/rootfs/etc/banana/hw/thor.conf`.

## A prompt for an AI agent on your PC

Paste this into Claude Code (or another coding agent) on a Linux PC with about 300 GB free and 32 GB RAM or more:

```text
Build the stripped LineageOS for the AYN Thor with BananaOS's launcher, up to a flashable build. Do not flash it.

Setup
- Repository: https://github.com/JorreFog/BananaLinux, branch claude/pensive-rubin-cy7ro4. Read docs/THOR-LINEAGE.md
  (this guide), docs/THOR-LINEAGE-PARITY.md, android/launcher/README.md, android/system/hwd/PROTOCOL.md first.
- Check the machine: Linux x86_64, ~300 GB free, 32 GB+ RAM, git-lfs installed. If it falls short, stop and say so.
- Build the launcher APK as android/launcher/README.md says and put the release APK at
  android/launcher/prebuilt/BananaLauncher.apk. Run android/system/emulators/fetch.sh for the emulator APKs.
- Follow this guide's steps exactly: repo init on the LineageOS branch it names, the local manifest
  android/manifest/banana.xml, sync, then `breakfast banana_thor` and `m bacon`, in the background, checking on it.

When it fails
- Fix the cause in this repository (android/product, android/system, android/launcher, android/manifest), never
  only inside the LineageOS tree. Keep the pinned device, kernel and vendor revisions unless one truly can't build,
  and then say why.
- Don't get green by removing BananaOS's pieces (BananaLauncher, banana-hwd, banana_emulators) or the strip lists'
  reasons; if a strip removal breaks the build, move it to the aggressive list with a note.

Git
- Commit each fix separately on the branch with a message saying what failed and why the change fixes it. No AI or
  model names in commits or files. Push only to claude/pensive-rubin-cy7ro4.

Report back when it finishes or is stuck
- Whether the image exists, its path and size.
- Each failure: package, the error, the fix, the commit.
- Anything removed or disabled, anything still failing, total build time.
- What was not verified. A finished image has only been compiled, never booted.
```

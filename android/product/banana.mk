#
# BananaOS's layer on LineageOS: what it adds, what it takes out, and how it is tuned.
# Inherited by lineage_banana_thor.mk after the device and LineageOS's common configuration.
#
# How things are taken out. PRODUCT_PACKAGES only ever grows as products inherit each other, so a package added by
# AOSP's handheld_*.mk or LineageOS's common*.mk can't be filtered out here. Instead the build's own mechanism is
# used: a module that "overrides" another stops the other from being installed (build/make/core/main.mk,
# product-installed-modules / module-overrides). strip/Android.mk defines two such modules, BananaStrip and
# BananaStripAggressive, that install nothing and only override. Every removal and why it is safe is listed there.
#

BANANA_PRODUCT_DIR := vendor/banana/android/product

# --- BananaOS ------------------------------------------------------------------------------------------------------

# Defined by the other parts of this repository (android/launcher and android/system):
#   BananaLauncher    the menu as an Android app (android_app_import, privileged). It is the only HOME activity
#                     left once BananaStrip removes Launcher3, so Android makes it the default HOME without asking,
#                     and it is the secondary-display HOME (overlay/BananaFrameworks).
#   banana-hwd        the hardware daemon (fan, stick lights, the AYN button, sleep), with its init .rc
#   banana_emulators  phony: the emulator APKs
PRODUCT_PACKAGES += \
    BananaLauncher \
    banana-hwd \
    banana_emulators

# What is removed (strip/Android.mk). Set BANANA_STRIP_AGGRESSIVE=true in the environment before "breakfast" for
# the second list too (telephony stack, print spooler, ...): lighter, less tested. See docs/THOR-LINEAGE.md.
PRODUCT_PACKAGES += \
    BananaStrip

ifeq ($(BANANA_STRIP_AGGRESSIVE),true)
PRODUCT_PACKAGES += \
    BananaStripAggressive \
    BananaFrameworksOverlayAggressive
endif

# Resource overlays (overlay/): secondary-display HOME, no screensavers, no lock screen by default, shorter
# animations, a short quick settings panel.
PRODUCT_PACKAGES += \
    BananaFrameworksOverlay \
    BananaSettingsProviderOverlay \
    BananaSystemUIOverlay

# AudioFX is only added by LineageOS's "full" configuration; say so anyway in case that changes.
TARGET_EXCLUDES_AUDIOFX := true

# --- System properties ---------------------------------------------------------------------------------------------

PRODUCT_PRODUCT_PROPERTIES += \
    ro.banana.product=thor

# Never a "low RAM" device: ro.config.low_ram=true makes ActivityManager, the GPU driver and games assume a
# 1-2 GB phone (smaller caches, no 120 Hz in some engines, apps refusing to run). The Thor has 8 to 16 GB.
PRODUCT_PRODUCT_PROPERTIES += \
    ro.config.low_ram=false

# Low-memory killer. lmkd stays on PSI (Android's default); the one change: when it has to kill, it kills the
# largest killable process of the lowest priority rather than the least recently used one, so one kill frees enough
# and a running game (foreground, never killable before cached apps) is less likely to stall behind several small
# kills. Everything else is left on LineageOS's defaults until measured on a Thor (docs/THOR-LINEAGE.md, "Memory").
PRODUCT_PRODUCT_PROPERTIES += \
    ro.lmk.kill_heaviest_task=true

# No first-run wizard of any kind (LineageSetupWizard is removed; AOSP's Provision marks setup done and disables
# itself; this property is read by GMS-style wizards only and is inert otherwise).
PRODUCT_PRODUCT_PROPERTIES += \
    ro.setupwizard.mode=DISABLED

# adb: on by default over USB (MTP plus adb), with authorization kept (ro.adb.secure=1, LineageOS's default for
# userdebug): the first connection shows the computer's key on the Thor to accept. For bring-up without a working
# screen, build with WITH_ADB_INSECURE=true (LineageOS's switch; never for a build given to others).
PRODUCT_SYSTEM_PROPERTIES += \
    persist.sys.usb.config=mtp,adb

# The device's dalvik heap (phone-xhdpi-6144-dalvik-heap.mk from qcs8550-common) is kept: it is a per-app ceiling,
# not a reservation, and the emulators' Java front ends and the launcher may need it.

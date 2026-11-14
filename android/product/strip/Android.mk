#
# BananaOS on LineageOS: what is taken out of the image.
#
# Each module below installs nothing (LOCAL_UNINSTALLABLE_MODULE) and lists packages in LOCAL_OVERRIDES_PACKAGES.
# When it is in PRODUCT_PACKAGES (banana.mk), the build drops every package it overrides from the image, even ones
# another makefile added (build/make/core/main.mk: product-installed-modules filters module-overrides). This is the
# pattern many LineageOS-based ROMs use for "remove packages"; it has NOT been built on lineage-24.0 yet. If the
# build ever refuses a prebuilt without a source, the fallback is to put the same list in BananaLauncher's
# "overrides:" (android_app_import supports it).
#
# Overrides chain: removing a package also undoes what IT overrode. Launcher3QuickStep overrides Launcher3 and
# QuickSearchBox (packages/apps/Launcher3/Android.bp), so all three are listed. LineageSetupWizard overrides
# Provision: with the wizard gone, AOSP's Provision comes back, which is what we want (it marks the device set up on
# first boot and disables itself; it is a few hundred KB and never runs again).
#

LOCAL_PATH := $(call my-dir)

# ---------------------------------------------------------------------------------------------------------------
# BananaStrip: safe removals. Nothing else in the image depends on these; each is an app a user opens, a
# screensaver, a wallpaper, or a provider only those apps use.
# ---------------------------------------------------------------------------------------------------------------
include $(CLEAR_VARS)
LOCAL_MODULE := BananaStrip
LOCAL_MODULE_CLASS := APPS
LOCAL_MODULE_TAGS := optional
LOCAL_UNINSTALLABLE_MODULE := true
LOCAL_CERTIFICATE := PRESIGNED
LOCAL_SRC_FILES := /dev/null
LOCAL_OVERRIDES_PACKAGES := \
    Launcher3QuickStep \
    Launcher3 \
    Launcher3QuickStepGo \
    QuickSearchBox \
    LineageSetupWizard \
    Updater \
    Jelly \
    ExactCalculator \
    EmergencyInfo \
    Backgrounds \
    Canvas \
    Glimpse \
    ThemePicker \
    QuickAccessWallet \
    DeskClock \
    BasicDreams \
    PhotoTable \
    LiveWallpapersPicker \
    WAPPushManager \
    EasterEgg \
    DeviceAsWebcam \
    CameraExtensionsProxy \
    MusicFX \
    PrintRecommendationService \
    BuiltInPrintService \
    BookmarkProvider \
    SimAppDialog
# Why each is safe:
#   Launcher3QuickStep, Launcher3, Launcher3QuickStepGo  HOME is BananaLauncher. Launcher3QuickStep is also Android's
#                         "recents" provider (config_recentsComponentName): without it the Overview/recents button
#                         and the gesture-navigation swipe-up do nothing. BananaLauncher shows what runs; the
#                         navigation bar's Home and Back keep working. (Risk noted in docs/THOR-LINEAGE.md.)
#   QuickSearchBox        a search widget for Launcher3.
#   LineageSetupWizard    first-run wizard; Provision (AOSP) replaces it and completes setup silently.
#   Updater               LineageOS's OTA client: it would offer LineageOS's own odin2thor builds, which are not
#                         BananaOS. Banana builds are installed with adb sideload (docs/THOR-LINEAGE.md).
#   Jelly                 the browser app. Android System WebView stays (apps and the launcher may embed it).
#   ExactCalculator, DeskClock (clock, alarms, timers), Glimpse (gallery), Canvas (photo editor),
#   EmergencyInfo, QuickAccessWallet (wallet tile)        apps nobody else calls.
#   Backgrounds, ThemePicker, LiveWallpapersPicker        wallpaper and style pickers; the launcher draws its own
#                         background. LineageBlackTheme (pure black dark theme) is kept for the AMOLED panels.
#   BasicDreams, PhotoTable  screensavers; dreams are also switched off (overlay/BananaFrameworks).
#   WAPPushManager, SimAppDialog  SMS/WAP push and SIM dialogs; the Thor has no modem.
#   DeviceAsWebcam, CameraExtensionsProxy  camera features; the Thor has no camera (PRODUCT_NO_CAMERA in
#                         qcs8550-common, "Camera: none" in LineageOS's device data).
#   MusicFX               the equalizer panel apps open for an audio session; nothing in the image needs it.
#   PrintRecommendationService, BuiltInPrintService  printer discovery and the default print service.
#   BookmarkProvider      browser bookmarks.
#   EasterEgg             the version easter egg.
include $(BUILD_PREBUILT)

# ---------------------------------------------------------------------------------------------------------------
# BananaStripAggressive: only with BANANA_STRIP_AGGRESSIVE=true. Lighter at idle (TeleService is a persistent
# process, the "phone" process, started at boot even without a modem), but each of these is something parts of the
# framework or Settings look for. Untested on the Thor: try it once the safe build boots, and check Settings
# (Network & internet, Accounts, Users, Printing) and an emulator or two for crashes.
# ---------------------------------------------------------------------------------------------------------------
include $(CLEAR_VARS)
LOCAL_MODULE := BananaStripAggressive
LOCAL_MODULE_CLASS := APPS
LOCAL_MODULE_TAGS := optional
LOCAL_UNINSTALLABLE_MODULE := true
LOCAL_CERTIFICATE := PRESIGNED
LOCAL_SRC_FILES := /dev/null
LOCAL_OVERRIDES_PACKAGES := \
    TeleService \
    TelephonyProvider \
    TelephonyProviderHsum \
    MmsService \
    CalendarProvider \
    ManagedProvisioning \
    PrintSpooler \
    SecureElement \
    PrivateSpace \
    AvatarPicker \
    AccessibilityMenu \
    WallpaperCropper \
    DeviceDiagnostics
# Why each is risky:
#   TeleService (the phone process), TelephonyProvider, MmsService  the Thor has no modem, and these do nothing
#                         useful, but system_server's telephony and subscription code and SystemUI's signal
#                         controllers expect the "phone" service. Android TV images run without them, a handheld
#                         image has not been tried. Watch for crashes in Settings > Network & internet and in
#                         SystemUI's status bar.
#   CalendarProvider      no calendar app is left; an app that writes events (some launchers, none of ours) fails.
#   ManagedProvisioning   work profiles and device-owner setup.
#   PrintSpooler          the print framework's spooler: an app calling PrintManager gets an error instead of a
#                         dialog.
#   SecureElement         the Thor has no NFC or eSE (no SE HAL in the vendor manifest is expected; verify).
#   PrivateSpace          Android's private space.
#   AvatarPicker          profile photo picker (Settings > Users).
#   AccessibilityMenu     the large accessibility menu; other accessibility services stay.
#   WallpaperCropper      used when setting a wallpaper from an app; goes with the wallpaper service being off
#                         (BananaFrameworksOverlayAggressive).
#   DeviceDiagnostics     diagnostics UI.
include $(BUILD_PREBUILT)

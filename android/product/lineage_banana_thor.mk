#
# BananaOS for the AYN Thor, on LineageOS 24.0 (Android 17).
#
# This is LineageOS's own product for the Thor (device/ayn/odin2thor/lineage_odin2thor.mk) with two changes:
#   1. LineageOS's "mini" wifi-only tablet configuration instead of the "full" one: the full one adds Etar
#      (calendar), Twelve (music), Recorder, Camelot (PDF), Seedvault (backup), Profiles and AudioFX, none of which
#      this handheld runs. The mini one keeps everything LineageOS's framework, Settings and SystemUI need.
#   2. BananaOS on top (banana.mk): the launcher, the hardware daemon, the emulators, and the removal of what is
#      left that a game handheld doesn't need.
#
# Keep the device lines identical to lineage_odin2thor.mk (check it after every sync): the device tree, the
# proprietary files and LineageOS's updater all key on PRODUCT_DEVICE odin2thor.
#

# Inherit from those products. Most specific first. (As lineage_odin2thor.mk.)
$(call inherit-product, $(SRC_TARGET_DIR)/product/core_64_bit_only.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/full_base.mk)

# The Thor: device/ayn/odin2thor/device.mk pulls in qcs8550-common, hardware/ayn and vendor/ayn/*.
$(call inherit-product, device/ayn/odin2thor/device.mk)

# LineageOS's common configuration: mini, tablet, wifi-only (lineage_odin2thor.mk uses
# common_full_tablet_wifionly.mk).
$(call inherit-product, vendor/lineage/config/common_mini_tablet_wifionly.mk)

# BananaOS.
$(call inherit-product, vendor/banana/android/product/banana.mk)

PRODUCT_NAME := lineage_banana_thor
PRODUCT_DEVICE := odin2thor
PRODUCT_MANUFACTURER := AYN
PRODUCT_BRAND := AYN
PRODUCT_MODEL := Thor
PRODUCT_SYSTEM_DEVICE := thor
PRODUCT_SYSTEM_NAME := Thor

PRODUCT_BUILD_PROP_OVERRIDES += \
    SystemDevice=Thor \
    SystemName=thor \
    DeviceProduct=odin2thor

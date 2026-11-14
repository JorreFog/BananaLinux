#
# BananaOS on LineageOS: board-level additions (things a product makefile can't set).
#
# Reached through LineageOS's hook: build/make/core/board_config.mk does "-include vendor/extra/BoardConfigExtra.mk"
# right after the device's BoardConfig.mk, and android/manifest/banana.xml links that path to this file. It is read
# for every product built in the tree, so it does nothing unless the product is BananaOS's.
#

ifeq ($(TARGET_PRODUCT),lineage_banana_thor)

BANANA_SEPOLICY := vendor/banana/android/system/sepolicy

# banana-hwd's SELinux policy (written in android/system/sepolicy). Whichever layout that directory uses is picked
# up: vendor/ for a daemon on /vendor, private/ and public/ for one on /system_ext, or the directory itself as
# vendor policy when it holds .te files directly. When the directory doesn't exist yet nothing is added, and the
# daemon would run in the wrong domain or be denied: check "adb shell dmesg | grep avc" on the first boot.
ifneq ($(wildcard $(BANANA_SEPOLICY)/vendor/*.te $(BANANA_SEPOLICY)/vendor/file_contexts),)
BOARD_VENDOR_SEPOLICY_DIRS += $(BANANA_SEPOLICY)/vendor
endif
ifneq ($(wildcard $(BANANA_SEPOLICY)/private/*.te $(BANANA_SEPOLICY)/private/file_contexts),)
SYSTEM_EXT_PRIVATE_SEPOLICY_DIRS += $(BANANA_SEPOLICY)/private
endif
ifneq ($(wildcard $(BANANA_SEPOLICY)/public/*.te),)
SYSTEM_EXT_PUBLIC_SEPOLICY_DIRS += $(BANANA_SEPOLICY)/public
endif
ifneq ($(wildcard $(BANANA_SEPOLICY)/*.te $(BANANA_SEPOLICY)/file_contexts),)
BOARD_VENDOR_SEPOLICY_DIRS += $(BANANA_SEPOLICY)
endif

endif

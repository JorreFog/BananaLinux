################################################################################
#
# libretro-dosbox-pure
#
################################################################################

# DOSBox Pure: DOS games straight from their .zip, with its own on-screen keyboard and mapper.
# Ported from ROCKNIX's dosbox-pure-lr (packages/emulators/libretro/dosbox-pure-lr at 6cf94dba, GPL-2.0):
# the same commit and build options.
LIBRETRO_DOSBOX_PURE_VERSION = cdaea10ce8cac231bc341ca05d4c5e4e5fe0fbd5
LIBRETRO_DOSBOX_PURE_SITE = $(call github,schellingb,dosbox-pure,$(LIBRETRO_DOSBOX_PURE_VERSION))
LIBRETRO_DOSBOX_PURE_LICENSE = GPL-2.0+
LIBRETRO_DOSBOX_PURE_LICENSE_FILES = LICENSE

define LIBRETRO_DOSBOX_PURE_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) platform=unix CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" AR="$(TARGET_AR)"
endef

define LIBRETRO_DOSBOX_PURE_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/dosbox_pure_libretro.so $(TARGET_DIR)/usr/lib/libretro/dosbox_pure_libretro.so
endef

$(eval $(generic-package))

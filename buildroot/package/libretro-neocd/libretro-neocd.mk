################################################################################
#
# libretro-neocd
#
################################################################################

# NeoCD, the Neo Geo CD (its BIOS in /data/bios/neocd).
# Ported from ROCKNIX's neocd_lr (packages/emulators/libretro/neocd_lr at 6cf94dba, GPL-2.0):
# the same commit and build options.
LIBRETRO_NEOCD_VERSION = b1e04c738cb48a1dae0574b8877f6a116d270ca1
LIBRETRO_NEOCD_SITE = $(call github,libretro,neocd_libretro,$(LIBRETRO_NEOCD_VERSION))
LIBRETRO_NEOCD_LICENSE = LGPL-3.0
LIBRETRO_NEOCD_LICENSE_FILES = LICENSE.md
LIBRETRO_NEOCD_DEPENDENCIES = flac libogg libvorbis

define LIBRETRO_NEOCD_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) platform=unix CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" AR="$(TARGET_AR)"
endef

define LIBRETRO_NEOCD_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/neocd_libretro.so $(TARGET_DIR)/usr/lib/libretro/neocd_libretro.so
endef

$(eval $(generic-package))

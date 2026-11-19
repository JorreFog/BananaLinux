################################################################################
#
# libretro-mame2003-plus
#
################################################################################

LIBRETRO_MAME2003_PLUS_VERSION = dff0ba63654f4e93a24182e8aa018b174357a3e8
LIBRETRO_MAME2003_PLUS_SITE = $(call github,libretro,mame2003-plus-libretro,$(LIBRETRO_MAME2003_PLUS_VERSION))
LIBRETRO_MAME2003_PLUS_LICENSE = MAME (non-commercial)
LIBRETRO_MAME2003_PLUS_LICENSE_FILES = LICENSE.md

define LIBRETRO_MAME2003_PLUS_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) platform=unix CC="$(TARGET_CC)" CXX="$(TARGET_CXX)"
endef

define LIBRETRO_MAME2003_PLUS_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/mame2003_plus_libretro.so $(TARGET_DIR)/usr/lib/libretro/mame2003_plus_libretro.so
endef

$(eval $(generic-package))

################################################################################
#
# libretro-beetle-pce
#
################################################################################

LIBRETRO_BEETLE_PCE_VERSION = b96c11e095b6a40a412d2da02766ae1c3f4fd539
LIBRETRO_BEETLE_PCE_SITE = $(call github,libretro,beetle-pce-libretro,$(LIBRETRO_BEETLE_PCE_VERSION))
LIBRETRO_BEETLE_PCE_LICENSE = GPL-2.0
LIBRETRO_BEETLE_PCE_LICENSE_FILES = COPYING

define LIBRETRO_BEETLE_PCE_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) platform=unix CC="$(TARGET_CC)" CXX="$(TARGET_CXX)"
endef

define LIBRETRO_BEETLE_PCE_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/mednafen_pce_libretro.so $(TARGET_DIR)/usr/lib/libretro/mednafen_pce_libretro.so
endef

$(eval $(generic-package))

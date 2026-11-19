################################################################################
#
# libretro-snes9x2010
#
################################################################################

LIBRETRO_SNES9X2010_VERSION = fe690dd321fa5a46b5234a2bde089d2518c62b0e
LIBRETRO_SNES9X2010_SITE = $(call github,libretro,snes9x2010,$(LIBRETRO_SNES9X2010_VERSION))
LIBRETRO_SNES9X2010_LICENSE = Snes9x (non-commercial)
LIBRETRO_SNES9X2010_LICENSE_FILES = LICENSE.txt

define LIBRETRO_SNES9X2010_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) platform=unix CC="$(TARGET_CC)" CXX="$(TARGET_CXX)"
endef

define LIBRETRO_SNES9X2010_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/snes9x2010_libretro.so $(TARGET_DIR)/usr/lib/libretro/snes9x2010_libretro.so
endef

$(eval $(generic-package))

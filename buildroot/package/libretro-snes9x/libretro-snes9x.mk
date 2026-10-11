################################################################################
#
# libretro-snes9x
#
################################################################################

LIBRETRO_SNES9X_VERSION = fae2fea08f74180759ef540ee94259213f503480
LIBRETRO_SNES9X_SITE = $(call github,libretro,snes9x,$(LIBRETRO_SNES9X_VERSION))
LIBRETRO_SNES9X_LICENSE = Snes9x (non-commercial)
LIBRETRO_SNES9X_LICENSE_FILES = LICENSE

define LIBRETRO_SNES9X_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D)/libretro platform=unix CC="$(TARGET_CC)" CXX="$(TARGET_CXX)"
endef

define LIBRETRO_SNES9X_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/libretro/snes9x_libretro.so $(TARGET_DIR)/usr/lib/libretro/snes9x_libretro.so
endef

$(eval $(generic-package))

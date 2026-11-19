################################################################################
#
# libretro-gearboy
#
################################################################################

LIBRETRO_GEARBOY_VERSION = c2beeea6e202a41f796c4026fa0caaee1b904662
LIBRETRO_GEARBOY_SITE = $(call github,drhelius,Gearboy,$(LIBRETRO_GEARBOY_VERSION))
LIBRETRO_GEARBOY_LICENSE = GPL-3.0
LIBRETRO_GEARBOY_LICENSE_FILES = LICENSE

define LIBRETRO_GEARBOY_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D)/platforms/libretro platform=unix CC="$(TARGET_CC)" CXX="$(TARGET_CXX)"
endef

define LIBRETRO_GEARBOY_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/platforms/libretro/gearboy_libretro.so $(TARGET_DIR)/usr/lib/libretro/gearboy_libretro.so
endef

$(eval $(generic-package))

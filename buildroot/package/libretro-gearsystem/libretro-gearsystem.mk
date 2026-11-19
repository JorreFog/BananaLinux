################################################################################
#
# libretro-gearsystem
#
################################################################################

LIBRETRO_GEARSYSTEM_VERSION = 1be9a12b82cae84ddd6f9b7006d546abe2d58bd4
LIBRETRO_GEARSYSTEM_SITE = $(call github,drhelius,Gearsystem,$(LIBRETRO_GEARSYSTEM_VERSION))
LIBRETRO_GEARSYSTEM_LICENSE = GPL-3.0
LIBRETRO_GEARSYSTEM_LICENSE_FILES = LICENSE

define LIBRETRO_GEARSYSTEM_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D)/platforms/libretro platform=unix CC="$(TARGET_CC)" CXX="$(TARGET_CXX)"
endef

define LIBRETRO_GEARSYSTEM_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/platforms/libretro/gearsystem_libretro.so $(TARGET_DIR)/usr/lib/libretro/gearsystem_libretro.so
endef

$(eval $(generic-package))

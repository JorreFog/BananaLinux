################################################################################
#
# libretro-geargrafx
#
################################################################################

LIBRETRO_GEARGRAFX_VERSION = c669129e3e24278798f6683ceb3298e16564994d
LIBRETRO_GEARGRAFX_SITE = $(call github,drhelius,Geargrafx,$(LIBRETRO_GEARGRAFX_VERSION))
LIBRETRO_GEARGRAFX_LICENSE = GPL-3.0
LIBRETRO_GEARGRAFX_LICENSE_FILES = LICENSE

define LIBRETRO_GEARGRAFX_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D)/platforms/libretro platform=unix CC="$(TARGET_CC)" CXX="$(TARGET_CXX)"
endef

define LIBRETRO_GEARGRAFX_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/platforms/libretro/geargrafx_libretro.so $(TARGET_DIR)/usr/lib/libretro/geargrafx_libretro.so
endef

$(eval $(generic-package))

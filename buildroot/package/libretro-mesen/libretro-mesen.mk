################################################################################
#
# libretro-mesen
#
################################################################################

LIBRETRO_MESEN_VERSION = 0102910c39ad1a62bc3f784466f3f67ca9eae335
LIBRETRO_MESEN_SITE = $(call github,libretro,Mesen,$(LIBRETRO_MESEN_VERSION))
LIBRETRO_MESEN_LICENSE = GPL-3.0
LIBRETRO_MESEN_LICENSE_FILES = LICENSE

define LIBRETRO_MESEN_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D)/Libretro platform=unix CC="$(TARGET_CC)" CXX="$(TARGET_CXX)"
endef

define LIBRETRO_MESEN_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/Libretro/mesen_libretro.so $(TARGET_DIR)/usr/lib/libretro/mesen_libretro.so
endef

$(eval $(generic-package))

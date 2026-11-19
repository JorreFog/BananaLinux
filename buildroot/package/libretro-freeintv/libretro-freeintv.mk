################################################################################
#
# libretro-freeintv
#
################################################################################

LIBRETRO_FREEINTV_VERSION = ef3e0fe322bec62a7f916c0bb0834c08c348d0b4
LIBRETRO_FREEINTV_SITE = $(call github,libretro,FreeIntv,$(LIBRETRO_FREEINTV_VERSION))
LIBRETRO_FREEINTV_LICENSE = GPL-2.0+
LIBRETRO_FREEINTV_LICENSE_FILES = LICENSE

define LIBRETRO_FREEINTV_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) platform=unix CC="$(TARGET_CC)" CXX="$(TARGET_CXX)"
endef

define LIBRETRO_FREEINTV_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/freeintv_libretro.so $(TARGET_DIR)/usr/lib/libretro/freeintv_libretro.so
endef

$(eval $(generic-package))

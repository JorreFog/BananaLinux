################################################################################
#
# libretro-beetle-psx
#
################################################################################

LIBRETRO_BEETLE_PSX_VERSION = 71ee81e77acd49296ecf4a683f5a61568ba80bf9
LIBRETRO_BEETLE_PSX_SITE = $(call github,libretro,beetle-psx-libretro,$(LIBRETRO_BEETLE_PSX_VERSION))
LIBRETRO_BEETLE_PSX_LICENSE = GPL-2.0
LIBRETRO_BEETLE_PSX_LICENSE_FILES = COPYING

define LIBRETRO_BEETLE_PSX_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) platform=unix CC="$(TARGET_CC)" CXX="$(TARGET_CXX)"
endef

define LIBRETRO_BEETLE_PSX_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/mednafen_psx_libretro.so $(TARGET_DIR)/usr/lib/libretro/mednafen_psx_libretro.so
endef

$(eval $(generic-package))

################################################################################
#
# libretro-beetle-lynx
#
################################################################################

LIBRETRO_BEETLE_LYNX_VERSION = fcdefcfb3c11d6d2e71be076a5d3df2e88ab73ed
LIBRETRO_BEETLE_LYNX_SITE = $(call github,libretro,beetle-lynx-libretro,$(LIBRETRO_BEETLE_LYNX_VERSION))
LIBRETRO_BEETLE_LYNX_LICENSE = GPL-2.0
LIBRETRO_BEETLE_LYNX_LICENSE_FILES = COPYING

define LIBRETRO_BEETLE_LYNX_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) platform=unix CC="$(TARGET_CC)" CXX="$(TARGET_CXX)"
endef

define LIBRETRO_BEETLE_LYNX_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/mednafen_lynx_libretro.so $(TARGET_DIR)/usr/lib/libretro/mednafen_lynx_libretro.so
endef

$(eval $(generic-package))

################################################################################
#
# libretro-beetle-vb
#
################################################################################

LIBRETRO_BEETLE_VB_VERSION = d5ae54c8e23b573a3b34e888b2d48a588027e92c
LIBRETRO_BEETLE_VB_SITE = $(call github,libretro,beetle-vb-libretro,$(LIBRETRO_BEETLE_VB_VERSION))
LIBRETRO_BEETLE_VB_LICENSE = GPL-2.0
LIBRETRO_BEETLE_VB_LICENSE_FILES = COPYING

define LIBRETRO_BEETLE_VB_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) platform=unix CC="$(TARGET_CC)" CXX="$(TARGET_CXX)"
endef

define LIBRETRO_BEETLE_VB_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/mednafen_vb_libretro.so $(TARGET_DIR)/usr/lib/libretro/mednafen_vb_libretro.so
endef

$(eval $(generic-package))

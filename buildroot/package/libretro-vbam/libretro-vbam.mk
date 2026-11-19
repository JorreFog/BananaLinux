################################################################################
#
# libretro-vbam
#
################################################################################

LIBRETRO_VBAM_VERSION = 115defb3a318258ab84746d45258a1aec19d0b4b
LIBRETRO_VBAM_SITE = $(call github,libretro,vbam-libretro,$(LIBRETRO_VBAM_VERSION))
LIBRETRO_VBAM_LICENSE = GPL-2.0
LIBRETRO_VBAM_LICENSE_FILES = doc/gpl.txt

define LIBRETRO_VBAM_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D)/src/libretro platform=unix CC="$(TARGET_CC)" CXX="$(TARGET_CXX)"
endef

define LIBRETRO_VBAM_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/src/libretro/vbam_libretro.so $(TARGET_DIR)/usr/lib/libretro/vbam_libretro.so
endef

$(eval $(generic-package))

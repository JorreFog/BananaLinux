################################################################################
#
# libretro-vecx
#
################################################################################

LIBRETRO_VECX_VERSION = dccff5402a16d1ea7c112095a8e42632bf149506
LIBRETRO_VECX_SITE = $(call github,libretro,libretro-vecx,$(LIBRETRO_VECX_VERSION))
LIBRETRO_VECX_LICENSE = GPL-3.0
LIBRETRO_VECX_LICENSE_FILES = LICENSE.md

# HAS_GPU=0: the vectors drawn in software, with no GL to link
define LIBRETRO_VECX_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) platform=unix HAS_GPU=0 CC="$(TARGET_CC)" CXX="$(TARGET_CXX)"
endef

define LIBRETRO_VECX_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/vecx_libretro.so $(TARGET_DIR)/usr/lib/libretro/vecx_libretro.so
endef

$(eval $(generic-package))

################################################################################
#
# libretro-vecx
#
################################################################################

# vecx, the Vectrex, its lines drawn by the GPU (GLES) as ROCKNIX builds it.
# Ported from ROCKNIX's vecx-lr (packages/emulators/libretro/vecx-lr at 6cf94dba, GPL-2.0):
# the same commit and build options.
LIBRETRO_VECX_VERSION = dccff5402a16d1ea7c112095a8e42632bf149506
LIBRETRO_VECX_SITE = $(call github,libretro,libretro-vecx,$(LIBRETRO_VECX_VERSION))
LIBRETRO_VECX_LICENSE = GPL-3.0+
LIBRETRO_VECX_LICENSE_FILES = LICENSE.md

define LIBRETRO_VECX_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) -f Makefile.libretro platform=unix HAS_GPU=1 HAS_GLES=1 CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" AR="$(TARGET_AR)"
endef

define LIBRETRO_VECX_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/vecx_libretro.so $(TARGET_DIR)/usr/lib/libretro/vecx_libretro.so
endef

$(eval $(generic-package))

################################################################################
#
# libretro-genesis-plus-gx
#
################################################################################

# ROCKNIX's genesis-plus-gx-lr: upstream libretro/Genesis-Plus-GX at ROCKNIX's
# pinned commit.
LIBRETRO_GENESIS_PLUS_GX_VERSION = b7e79b3641eb2e322c5ba29903352e115887e83c
LIBRETRO_GENESIS_PLUS_GX_SITE = $(call github,libretro,Genesis-Plus-GX,$(LIBRETRO_GENESIS_PLUS_GX_VERSION))
LIBRETRO_GENESIS_PLUS_GX_LICENSE = Non-commercial
LIBRETRO_GENESIS_PLUS_GX_LICENSE_FILES = LICENSE.txt

define LIBRETRO_GENESIS_PLUS_GX_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) -f Makefile.libretro platform=unix \
		CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" AR="$(TARGET_AR)"
endef

define LIBRETRO_GENESIS_PLUS_GX_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/genesis_plus_gx_libretro.so \
		$(TARGET_DIR)/usr/lib/libretro/genesis_plus_gx_libretro.so
endef

$(eval $(generic-package))

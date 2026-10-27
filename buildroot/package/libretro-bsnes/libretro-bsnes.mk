################################################################################
#
# libretro-bsnes
#
################################################################################

# ROCKNIX's bsnes-lr: upstream libretro/bsnes-libretro at ROCKNIX's pinned commit.
LIBRETRO_BSNES_VERSION = b102d6d5817b25aa059b573cd3b7675f2e375fa4
LIBRETRO_BSNES_SITE = $(call github,libretro,bsnes-libretro,$(LIBRETRO_BSNES_VERSION))
LIBRETRO_BSNES_LICENSE = GPL-3.0+
LIBRETRO_BSNES_LICENSE_FILES = LICENSE

define LIBRETRO_BSNES_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) \
		CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" AR="$(TARGET_AR)"
endef

define LIBRETRO_BSNES_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/bsnes_libretro.so \
		$(TARGET_DIR)/usr/lib/libretro/bsnes_libretro.so
endef

$(eval $(generic-package))

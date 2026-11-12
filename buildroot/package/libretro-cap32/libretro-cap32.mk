################################################################################
#
# libretro-cap32
#
################################################################################

# Caprice32, the other Amstrad CPC core (the choice in Settings).
# Ported from ROCKNIX's cap32-lr (packages/emulators/libretro/cap32-lr at 6cf94dba, GPL-2.0):
# the same commit and build options.
LIBRETRO_CAP32_VERSION = 06ad0da5d003ab8f2359300ebce4cf31d4c55b6e
LIBRETRO_CAP32_SITE = $(call github,libretro,libretro-cap32,$(LIBRETRO_CAP32_VERSION))
LIBRETRO_CAP32_LICENSE = GPL-2.0+
LIBRETRO_CAP32_LICENSE_FILES = cap32/COPYING.txt

define LIBRETRO_CAP32_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) platform=unix CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" AR="$(TARGET_AR)"
endef

define LIBRETRO_CAP32_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/cap32_libretro.so $(TARGET_DIR)/usr/lib/libretro/cap32_libretro.so
endef

$(eval $(generic-package))

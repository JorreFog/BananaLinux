################################################################################
#
# libretro-nestopia
#
################################################################################

# Nestopia UE, ROCKNIX's default NES core (BananaNES stays the shelf's default here; this one is the choice in Settings).
# Ported from ROCKNIX's nestopia-lr (packages/emulators/libretro/nestopia-lr at 6cf94dba, GPL-2.0):
# the same commit and build options.
LIBRETRO_NESTOPIA_VERSION = f1e02d12664834536edbf7352995a3d0cd1caef4
LIBRETRO_NESTOPIA_SITE = $(call github,libretro,nestopia,$(LIBRETRO_NESTOPIA_VERSION))
LIBRETRO_NESTOPIA_LICENSE = GPL-2.0+
LIBRETRO_NESTOPIA_LICENSE_FILES = COPYING

define LIBRETRO_NESTOPIA_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D)/libretro platform=unix CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" AR="$(TARGET_AR)"
endef

define LIBRETRO_NESTOPIA_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/libretro/nestopia_libretro.so $(TARGET_DIR)/usr/lib/libretro/nestopia_libretro.so
endef

$(eval $(generic-package))

################################################################################
#
# libretro-mame2003-plus
#
################################################################################

# MAME 2003-Plus (0.78 romsets), the arcade shelf's second core. MAME's licence of that time: non-commercial.
# Ported from ROCKNIX's mame2003-plus-lr (packages/emulators/libretro/mame2003-plus-lr at 6cf94dba, GPL-2.0):
# the same commit and build options.
LIBRETRO_MAME2003_PLUS_VERSION = e455f1783a9a3a3cd00d334e39ae35aaf33b1128
LIBRETRO_MAME2003_PLUS_SITE = $(call github,libretro,mame2003-plus-libretro,$(LIBRETRO_MAME2003_PLUS_VERSION))
LIBRETRO_MAME2003_PLUS_LICENSE = MAME (non-commercial)
LIBRETRO_MAME2003_PLUS_LICENSE_FILES = LICENSE.md

define LIBRETRO_MAME2003_PLUS_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) platform=unix CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" AR="$(TARGET_AR)"
endef

define LIBRETRO_MAME2003_PLUS_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/mame2003_plus_libretro.so $(TARGET_DIR)/usr/lib/libretro/mame2003_plus_libretro.so
endef

$(eval $(generic-package))

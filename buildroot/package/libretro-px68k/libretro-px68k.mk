################################################################################
#
# libretro-px68k
#
################################################################################

# PX68k, the Sharp X68000 (/data/bios/keropi: iplrom.dat, cgrom.dat).
# Ported from ROCKNIX's px68k-lr (packages/emulators/libretro/px68k-lr at 6cf94dba, GPL-2.0):
# the same commit and build options.
LIBRETRO_PX68K_VERSION = d22f6a2e9111754e88a72b49b292c238fd32dc61
LIBRETRO_PX68K_SITE = $(call github,libretro,px68k-libretro,$(LIBRETRO_PX68K_VERSION))
LIBRETRO_PX68K_LICENSE = GPL-2.0+
LIBRETRO_PX68K_LICENSE_FILES = COPYING

define LIBRETRO_PX68K_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) platform=unix CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" AR="$(TARGET_AR)"
endef

define LIBRETRO_PX68K_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/px68k_libretro.so $(TARGET_DIR)/usr/lib/libretro/px68k_libretro.so
endef

$(eval $(generic-package))

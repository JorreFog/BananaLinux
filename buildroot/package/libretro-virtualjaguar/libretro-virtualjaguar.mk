################################################################################
#
# libretro-virtualjaguar
#
################################################################################

# Virtual Jaguar, the Atari Jaguar: the only Jaguar emulator for aarch64 Linux that ROCKNIX ships free (BigPEmu is a
# closed binary, not packaged). Many games run with faults.
# Ported from ROCKNIX's virtualjaguar-lr (packages/emulators/libretro/virtualjaguar-lr at 6cf94dba, GPL-2.0):
# the same commit and build options.
LIBRETRO_VIRTUALJAGUAR_VERSION = ffea7893a59dd9b9e6f9b85c51627810338b0778
LIBRETRO_VIRTUALJAGUAR_SITE = $(call github,libretro,virtualjaguar-libretro,$(LIBRETRO_VIRTUALJAGUAR_VERSION))
LIBRETRO_VIRTUALJAGUAR_LICENSE = GPL-3.0+
LIBRETRO_VIRTUALJAGUAR_LICENSE_FILES = LICENSE

define LIBRETRO_VIRTUALJAGUAR_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) platform=unix CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" AR="$(TARGET_AR)"
endef

define LIBRETRO_VIRTUALJAGUAR_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/virtualjaguar_libretro.so $(TARGET_DIR)/usr/lib/libretro/virtualjaguar_libretro.so
endef

$(eval $(generic-package))

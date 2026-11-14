################################################################################
#
# libretro-handy
#
################################################################################

# Handy, the Atari Lynx. ROCKNIX patches in an "emuelec" platform for aarch64; libretro's unix platform builds the
# same thing here without it.
# Ported from ROCKNIX's handy-lr (packages/emulators/libretro/handy-lr at 6cf94dba, GPL-2.0):
# the same commit and build options.
LIBRETRO_HANDY_VERSION = bc55d462f0b2d6b073ea93dc552ebd73cec60fd1
LIBRETRO_HANDY_SITE = $(call github,libretro,libretro-handy,$(LIBRETRO_HANDY_VERSION))
LIBRETRO_HANDY_LICENSE = Zlib
LIBRETRO_HANDY_LICENSE_FILES = lynx/license.txt

define LIBRETRO_HANDY_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) platform=unix CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" AR="$(TARGET_AR)"
endef

define LIBRETRO_HANDY_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/handy_libretro.so $(TARGET_DIR)/usr/lib/libretro/handy_libretro.so
endef

$(eval $(generic-package))

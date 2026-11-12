################################################################################
#
# libretro-stella
#
################################################################################

# Stella, the Atari 2600.
# Ported from ROCKNIX's stella-lr (packages/emulators/libretro/stella-lr at 6cf94dba, GPL-2.0):
# the same commit, build options and patches.
LIBRETRO_STELLA_VERSION = 514efcfea1216d43b0ed9068669a5d456e011c26
LIBRETRO_STELLA_SITE = $(call github,stella-emu,stella,$(LIBRETRO_STELLA_VERSION))
LIBRETRO_STELLA_LICENSE = GPL-2.0+
LIBRETRO_STELLA_LICENSE_FILES = License.txt
# Patches (beside this file, applied by Buildroot in order; each says where it comes from):
# - 0001-aarch64.patch (ROCKNIX's 001-aarch64.patch): an aarch64 platform in the libretro Makefile

define LIBRETRO_STELLA_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D)/src/os/libretro platform=aarch64 CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" AR="$(TARGET_AR)"
endef

define LIBRETRO_STELLA_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/src/os/libretro/stella_libretro.so $(TARGET_DIR)/usr/lib/libretro/stella_libretro.so
endef

$(eval $(generic-package))

################################################################################
#
# libretro-a5200
#
################################################################################

# a5200, the Atari 5200 (Atari800's 5200 part).
# Ported from ROCKNIX's a5200-lr (packages/emulators/libretro/a5200-lr at 6cf94dba, GPL-2.0):
# the same commit and build options.
LIBRETRO_A5200_VERSION = 40c6f2f1ad4a3145b328d5baaf010fae6c7e752b
LIBRETRO_A5200_SITE = $(call github,libretro,a5200,$(LIBRETRO_A5200_VERSION))
LIBRETRO_A5200_LICENSE = GPL-2.0+
LIBRETRO_A5200_LICENSE_FILES = License.txt

define LIBRETRO_A5200_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) platform=unix CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" AR="$(TARGET_AR)"
endef

define LIBRETRO_A5200_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/a5200_libretro.so $(TARGET_DIR)/usr/lib/libretro/a5200_libretro.so
endef

$(eval $(generic-package))

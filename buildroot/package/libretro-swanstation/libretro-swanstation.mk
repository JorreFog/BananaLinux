################################################################################
#
# libretro-swanstation
#
################################################################################

LIBRETRO_SWANSTATION_VERSION = 631f0fb31b809e0f889d7c551fe695788e39c1d3
LIBRETRO_SWANSTATION_SITE = $(call github,libretro,swanstation,$(LIBRETRO_SWANSTATION_VERSION))
LIBRETRO_SWANSTATION_LICENSE = GPL-3.0
LIBRETRO_SWANSTATION_LICENSE_FILES = LICENSE

# CPU_ARCH: its makefile otherwise takes the build machine's (x86 on CI, so the x86 recompiler)
define LIBRETRO_SWANSTATION_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) platform=unix -f Makefile.libretro CPU_ARCH=aarch64 CC="$(TARGET_CC)" CXX="$(TARGET_CXX)"
endef

define LIBRETRO_SWANSTATION_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/swanstation_libretro.so $(TARGET_DIR)/usr/lib/libretro/swanstation_libretro.so
endef

$(eval $(generic-package))

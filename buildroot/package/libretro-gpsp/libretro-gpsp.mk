################################################################################
#
# libretro-gpsp
#
################################################################################

LIBRETRO_GPSP_VERSION = 5819380c2ffb0900219d700a382ee68c464ebb99
LIBRETRO_GPSP_SITE = $(call github,libretro,gpsp,$(LIBRETRO_GPSP_VERSION))
LIBRETRO_GPSP_LICENSE = GPL-2.0
LIBRETRO_GPSP_LICENSE_FILES = COPYING

define LIBRETRO_GPSP_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) platform=unix CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" CPU_ARCH=arm64 HAVE_DYNAREC=1 MMAP_JIT_CACHE=1
endef

define LIBRETRO_GPSP_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/gpsp_libretro.so $(TARGET_DIR)/usr/lib/libretro/gpsp_libretro.so
endef

$(eval $(generic-package))

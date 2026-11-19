################################################################################
#
# libretro-parallel-n64
#
################################################################################

LIBRETRO_PARALLEL_N64_VERSION = 68d0ee3c8aea5e7ceb4c766afb9a8a8fc96eae6d
LIBRETRO_PARALLEL_N64_SITE = $(call github,libretro,parallel-n64,$(LIBRETRO_PARALLEL_N64_VERSION))
LIBRETRO_PARALLEL_N64_LICENSE = GPL-2.0
LIBRETRO_PARALLEL_N64_LICENSE_FILES = mupen64plus-core/LICENSES
# (its GLideN64 renderer draws with GLES)
LIBRETRO_PARALLEL_N64_DEPENDENCIES = libgles

define LIBRETRO_PARALLEL_N64_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) platform=unix WITH_DYNAREC=aarch64 GLES=1 HAVE_PARALLEL=0 HAVE_PARALLEL_RSP=0 CC="$(TARGET_CC)" CXX="$(TARGET_CXX)"
endef

define LIBRETRO_PARALLEL_N64_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/parallel_n64_libretro.so $(TARGET_DIR)/usr/lib/libretro/parallel_n64_libretro.so
endef

$(eval $(generic-package))

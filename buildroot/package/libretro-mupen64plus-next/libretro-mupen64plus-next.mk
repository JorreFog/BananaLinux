################################################################################
#
# libretro-mupen64plus-next
#
################################################################################

LIBRETRO_MUPEN64PLUS_NEXT_VERSION = 12edd2c74a517ff86dfa8cfc71ad75e4c10486d5
LIBRETRO_MUPEN64PLUS_NEXT_SITE = $(call github,libretro,mupen64plus-libretro-nx,$(LIBRETRO_MUPEN64PLUS_NEXT_VERSION))
LIBRETRO_MUPEN64PLUS_NEXT_LICENSE = GPL-2.0
LIBRETRO_MUPEN64PLUS_NEXT_LICENSE_FILES = LICENSE

define LIBRETRO_MUPEN64PLUS_NEXT_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) platform=unix ARCH=aarch64 WITH_DYNAREC=aarch64 FORCE_GLES3=1 HAVE_PARALLEL_RSP=0 CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" AS="$(TARGET_AS)"
endef

define LIBRETRO_MUPEN64PLUS_NEXT_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/mupen64plus_next_libretro.so $(TARGET_DIR)/usr/lib/libretro/mupen64plus_next_libretro.so
endef

$(eval $(generic-package))

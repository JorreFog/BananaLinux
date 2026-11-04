################################################################################
#
# libretro-fuse
#
################################################################################

# Fuse, the ZX Spectrum emulator, as a libretro core (systems.conf's [zxspectrum]).
# It carries libretro's on-screen keyboard of its own (SELECT opens it), which BananaFront's
# keyboard (F + B) and RetroArch's both fall back on for a game that asks for keys mid-play.
LIBRETRO_FUSE_VERSION = e997e2bc32c888348f862f69f2c53babfedf7791
LIBRETRO_FUSE_SITE = $(call github,libretro,fuse-libretro,$(LIBRETRO_FUSE_VERSION))
LIBRETRO_FUSE_LICENSE = GPL-3.0
LIBRETRO_FUSE_LICENSE_FILES = LICENSE

define LIBRETRO_FUSE_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) platform=unix \
		CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" AS="$(TARGET_AS)"
endef

define LIBRETRO_FUSE_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/fuse_libretro.so $(TARGET_DIR)/usr/lib/libretro/fuse_libretro.so
endef

$(eval $(generic-package))

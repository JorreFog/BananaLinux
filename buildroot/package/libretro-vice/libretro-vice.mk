################################################################################
#
# libretro-vice
#
################################################################################

# VICE's libretro port: three cores from one tree (x64sc, x64, x64dtv). x64sc is the cycle-exact
# emulator and BananaOS's default for the Commodore 64 (systems.conf's [c64]); it is the one built here.
# The Commodore 64 is a PAL machine (50 Hz): BananaFront paces it by time on the 60 Hz panel and RetroArch
# has its vsync switch, as both do for the other 50 Hz systems.
LIBRETRO_VICE_VERSION = 5ebe7d333ab15be57a9dde5f46c29a5bbd5f44f8
LIBRETRO_VICE_SITE = $(call github,libretro,vice-libretro,$(LIBRETRO_VICE_VERSION))
LIBRETRO_VICE_LICENSE = GPL-2.0+
LIBRETRO_VICE_LICENSE_FILES = COPYING
LIBRETRO_VICE_DEPENDENCIES = host-pkgconf

define LIBRETRO_VICE_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) EMUTYPE=x64sc platform=unix \
		CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" AS="$(TARGET_AS)"
endef

define LIBRETRO_VICE_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/vice_x64sc_libretro.so $(TARGET_DIR)/usr/lib/libretro/vice_x64sc_libretro.so
endef

$(eval $(generic-package))

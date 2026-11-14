################################################################################
#
# libretro-beetle-wswan
#
################################################################################

# Beetle Cygne (Mednafen's WonderSwan and WonderSwan Color).
# Ported from ROCKNIX's beetle-wswan-lr (packages/emulators/libretro/beetle-wswan-lr at 6cf94dba, GPL-2.0):
# the same commit and build options.
LIBRETRO_BEETLE_WSWAN_VERSION = 4b01295838ea89e3f1355bbe4cb5cf98aa6108cd
LIBRETRO_BEETLE_WSWAN_SITE = $(call github,libretro,beetle-wswan-libretro,$(LIBRETRO_BEETLE_WSWAN_VERSION))
LIBRETRO_BEETLE_WSWAN_LICENSE = GPL-2.0+
LIBRETRO_BEETLE_WSWAN_LICENSE_FILES = COPYING

define LIBRETRO_BEETLE_WSWAN_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) platform=unix CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" AR="$(TARGET_AR)"
endef

define LIBRETRO_BEETLE_WSWAN_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/mednafen_wswan_libretro.so $(TARGET_DIR)/usr/lib/libretro/mednafen_wswan_libretro.so
endef

$(eval $(generic-package))

################################################################################
#
# libretro-beetle-ngp
#
################################################################################

# Beetle NeoPop (Mednafen's Neo Geo Pocket and Pocket Color).
# Ported from ROCKNIX's beetle-ngp-lr (packages/emulators/libretro/beetle-ngp-lr at 6cf94dba, GPL-2.0):
# the same commit and build options.
LIBRETRO_BEETLE_NGP_VERSION = a50d5ac288a81f2104ddf43195a4efdd15c72227
LIBRETRO_BEETLE_NGP_SITE = $(call github,libretro,beetle-ngp-libretro,$(LIBRETRO_BEETLE_NGP_VERSION))
LIBRETRO_BEETLE_NGP_LICENSE = GPL-2.0+
LIBRETRO_BEETLE_NGP_LICENSE_FILES = COPYING

define LIBRETRO_BEETLE_NGP_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) platform=unix CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" AR="$(TARGET_AR)"
endef

define LIBRETRO_BEETLE_NGP_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/mednafen_ngp_libretro.so $(TARGET_DIR)/usr/lib/libretro/mednafen_ngp_libretro.so
endef

$(eval $(generic-package))

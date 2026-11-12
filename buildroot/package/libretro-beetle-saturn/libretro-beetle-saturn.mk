################################################################################
#
# libretro-beetle-saturn
#
################################################################################

# Beetle Saturn (Mednafen's Saturn): accurate, CPU-heavy, the X3 core's job. ROCKNIX passes platform=armv, which its
# Makefile only turns into unix; unix is passed here.
# Ported from ROCKNIX's beetle-saturn-lr (packages/emulators/libretro/beetle-saturn-lr at 6cf94dba, GPL-2.0):
# the same commit and build options.
LIBRETRO_BEETLE_SATURN_VERSION = fcb42b696fbca91d021bc9f11d4c288e56b9c20e
LIBRETRO_BEETLE_SATURN_SITE = $(call github,libretro,beetle-saturn-libretro,$(LIBRETRO_BEETLE_SATURN_VERSION))
LIBRETRO_BEETLE_SATURN_LICENSE = GPL-2.0+
LIBRETRO_BEETLE_SATURN_LICENSE_FILES = COPYING

define LIBRETRO_BEETLE_SATURN_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) platform=unix CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" AR="$(TARGET_AR)"
endef

define LIBRETRO_BEETLE_SATURN_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/mednafen_saturn_libretro.so $(TARGET_DIR)/usr/lib/libretro/mednafen_saturn_libretro.so
endef

$(eval $(generic-package))

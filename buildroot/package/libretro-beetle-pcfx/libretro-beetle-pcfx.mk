################################################################################
#
# libretro-beetle-pcfx
#
################################################################################

# Beetle PC-FX (Mednafen's PC-FX; pcfx.rom in /data/bios). platform=armv as ROCKNIX passes it: the Makefile's "unix"
# asks the build machine what it is (uname -p) and turns its x86 assembly on when that is a PC, whatever the target.
# Ported from ROCKNIX's beetle-pcfx-lr (packages/emulators/libretro/beetle-pcfx-lr at 6cf94dba, GPL-2.0):
# the same commit and build options.
LIBRETRO_BEETLE_PCFX_VERSION = c1650bad5fbdcc3c3ccd45e29e3983e1ccf16cff
LIBRETRO_BEETLE_PCFX_SITE = $(call github,libretro,beetle-pcfx-libretro,$(LIBRETRO_BEETLE_PCFX_VERSION))
LIBRETRO_BEETLE_PCFX_LICENSE = GPL-2.0+
LIBRETRO_BEETLE_PCFX_LICENSE_FILES = COPYING

define LIBRETRO_BEETLE_PCFX_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) platform=armv CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" AR="$(TARGET_AR)"
endef

define LIBRETRO_BEETLE_PCFX_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/mednafen_pcfx_libretro.so $(TARGET_DIR)/usr/lib/libretro/mednafen_pcfx_libretro.so
endef

$(eval $(generic-package))

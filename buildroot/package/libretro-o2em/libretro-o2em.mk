################################################################################
#
# libretro-o2em
#
################################################################################

# O2EM, the Magnavox Odyssey2 / Philips Videopac (o2rom.bin in /data/bios).
# Ported from ROCKNIX's o2em-lr (packages/emulators/libretro/o2em-lr at 6cf94dba, GPL-2.0):
# the same commit and build options.
LIBRETRO_O2EM_VERSION = 679d6fec04963f6e70a7ec217e3d0ebb1fe472fc
LIBRETRO_O2EM_SITE = $(call github,libretro,libretro-o2em,$(LIBRETRO_O2EM_VERSION))
LIBRETRO_O2EM_LICENSE = Artistic-2.0
LIBRETRO_O2EM_LICENSE_FILES = COPYING

define LIBRETRO_O2EM_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) platform=unix CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" AR="$(TARGET_AR)"
endef

define LIBRETRO_O2EM_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/o2em_libretro.so $(TARGET_DIR)/usr/lib/libretro/o2em_libretro.so
endef

$(eval $(generic-package))

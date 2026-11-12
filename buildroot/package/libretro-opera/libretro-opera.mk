################################################################################
#
# libretro-opera
#
################################################################################

# Opera, the 3DO (a 3DO BIOS such as panafz10.bin in /data/bios). Its code is LGPL with parts under non-commercial
# terms (its README says which).
# Ported from ROCKNIX's opera-lr (packages/emulators/libretro/opera-lr at 6cf94dba, GPL-2.0):
# the same commit and build options.
LIBRETRO_OPERA_VERSION = 54fe4f3754e9b69c83ff50b5def5d6c60fef06ce
LIBRETRO_OPERA_SITE = $(call github,libretro,opera-libretro,$(LIBRETRO_OPERA_VERSION))
LIBRETRO_OPERA_LICENSE = LGPL-2.1 (with non-commercial notes)
LIBRETRO_OPERA_LICENSE_FILES = README.md

define LIBRETRO_OPERA_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) platform=unix CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" AR="$(TARGET_AR)"
endef

define LIBRETRO_OPERA_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/opera_libretro.so $(TARGET_DIR)/usr/lib/libretro/opera_libretro.so
endef

$(eval $(generic-package))

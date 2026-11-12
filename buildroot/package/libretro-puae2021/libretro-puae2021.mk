################################################################################
#
# libretro-puae2021
#
################################################################################

# PUAE 2021 (libretro-uae's 2021 branch), ROCKNIX's default Amiga core on the SM8550. Installed as puae2021, as libretro
# names it, so the newer PUAE can sit beside it. Kickstart ROMs (kick34005.A500 ...) in /data/bios; AROS without them.
# Ported from ROCKNIX's puae2021-lr (packages/emulators/libretro/puae2021-lr at 6cf94dba, GPL-2.0):
# the same commit and build options.
LIBRETRO_PUAE2021_VERSION = 96ebfcfc2c66233ad37f6dc99ee991211dc719ad
LIBRETRO_PUAE2021_SITE = $(call github,libretro,libretro-uae,$(LIBRETRO_PUAE2021_VERSION))
LIBRETRO_PUAE2021_LICENSE = GPL-2.0+
LIBRETRO_PUAE2021_LICENSE_FILES = COPYING

define LIBRETRO_PUAE2021_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) platform=unix CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" AR="$(TARGET_AR)"
endef

define LIBRETRO_PUAE2021_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/puae_libretro.so $(TARGET_DIR)/usr/lib/libretro/puae2021_libretro.so
endef

$(eval $(generic-package))

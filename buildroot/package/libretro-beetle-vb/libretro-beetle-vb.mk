################################################################################
#
# libretro-beetle-vb
#
################################################################################

# Mednafen's Virtual Boy (Beetle VB). ROCKNIX installs it renamed beetle_vb; here it keeps libretro's own name, as
# libretro's core info and the Core Downloader know it.
# Ported from ROCKNIX's beetle-vb-lr (packages/emulators/libretro/beetle-vb-lr at 6cf94dba, GPL-2.0):
# the same commit and build options.
LIBRETRO_BEETLE_VB_VERSION = d5ae54c8e23b573a3b34e888b2d48a588027e92c
LIBRETRO_BEETLE_VB_SITE = $(call github,libretro,beetle-vb-libretro,$(LIBRETRO_BEETLE_VB_VERSION))
LIBRETRO_BEETLE_VB_LICENSE = GPL-2.0+
LIBRETRO_BEETLE_VB_LICENSE_FILES = COPYING

define LIBRETRO_BEETLE_VB_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) platform=unix CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" AR="$(TARGET_AR)"
endef

define LIBRETRO_BEETLE_VB_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/mednafen_vb_libretro.so $(TARGET_DIR)/usr/lib/libretro/mednafen_vb_libretro.so
endef

$(eval $(generic-package))

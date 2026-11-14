################################################################################
#
# libretro-bluemsx
#
################################################################################

# blueMSX: MSX, MSX2, MSX2+ (blueMSX's Machines and Databases folders go in /data/bios, from the core's own package).
# Ported from ROCKNIX's bluemsx-lr (packages/emulators/libretro/bluemsx-lr at 6cf94dba, GPL-2.0):
# the same commit and build options.
LIBRETRO_BLUEMSX_VERSION = e3086eb5d36d77fa11704cf53dc176686e70127d
LIBRETRO_BLUEMSX_SITE = $(call github,libretro,blueMSX-libretro,$(LIBRETRO_BLUEMSX_VERSION))
LIBRETRO_BLUEMSX_LICENSE = GPL-2.0+
LIBRETRO_BLUEMSX_LICENSE_FILES = license.txt

define LIBRETRO_BLUEMSX_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) -f Makefile.libretro platform=unix CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" AR="$(TARGET_AR)"
endef

define LIBRETRO_BLUEMSX_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/bluemsx_libretro.so $(TARGET_DIR)/usr/lib/libretro/bluemsx_libretro.so
endef

$(eval $(generic-package))

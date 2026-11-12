################################################################################
#
# libretro-gearcoleco
#
################################################################################

# Gearcoleco, the ColecoVision (colecovision.rom in /data/bios).
# Ported from ROCKNIX's gearcoleco-lr (packages/emulators/libretro/gearcoleco-lr at 6cf94dba, GPL-2.0):
# the same commit and build options.
LIBRETRO_GEARCOLECO_VERSION = c1ad731ee2211a5477a1a05cd84684de430169b3
LIBRETRO_GEARCOLECO_SITE = $(call github,drhelius,Gearcoleco,$(LIBRETRO_GEARCOLECO_VERSION))
LIBRETRO_GEARCOLECO_LICENSE = GPL-3.0+
LIBRETRO_GEARCOLECO_LICENSE_FILES = LICENSE

define LIBRETRO_GEARCOLECO_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D)/platforms/libretro platform=unix CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" AR="$(TARGET_AR)"
endef

define LIBRETRO_GEARCOLECO_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/platforms/libretro/gearcoleco_libretro.so $(TARGET_DIR)/usr/lib/libretro/gearcoleco_libretro.so
endef

$(eval $(generic-package))

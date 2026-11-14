################################################################################
#
# libretro-pokemini
#
################################################################################

# PokeMini, the Pokemon mini (a free BIOS of its own; a real bios.min in /data/bios is used when there).
# Ported from ROCKNIX's pokemini-lr (packages/emulators/libretro/pokemini-lr at 6cf94dba, GPL-2.0):
# the same commit and build options.
LIBRETRO_POKEMINI_VERSION = 132111b76343559860532a1ccc094f93f1ed5650
LIBRETRO_POKEMINI_SITE = $(call github,libretro,pokemini,$(LIBRETRO_POKEMINI_VERSION))
LIBRETRO_POKEMINI_LICENSE = GPL-3.0+
LIBRETRO_POKEMINI_LICENSE_FILES = LICENSE

define LIBRETRO_POKEMINI_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) platform=unix CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" AR="$(TARGET_AR)"
endef

define LIBRETRO_POKEMINI_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/pokemini_libretro.so $(TARGET_DIR)/usr/lib/libretro/pokemini_libretro.so
endef

$(eval $(generic-package))

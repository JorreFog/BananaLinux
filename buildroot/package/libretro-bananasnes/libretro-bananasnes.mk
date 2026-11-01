################################################################################
#
# libretro-bananasnes
#
################################################################################

# BananaSNES: our Snes9x fork for the RG DS / RG DS Plus (https://github.com/JorreFog/BananaSNES, branch banana/main).
# Same licence as Snes9x (free distribution only); the core is installed as bananasnes_libretro.so next to snes9x's.
# Pin: the commit of the BananaSNES release the 0.5.1 build was tested with (the repo is pushed at the end of the run).
LIBRETRO_BANANASNES_VERSION = 0000000000000000000000000000000000000000
LIBRETRO_BANANASNES_SITE = $(call github,JorreFog,BananaSNES,$(LIBRETRO_BANANASNES_VERSION))
LIBRETRO_BANANASNES_LICENSE = Snes9x (non-commercial)
LIBRETRO_BANANASNES_LICENSE_FILES = LICENSE

# BANANASNES_FLAGS: the optimisation set measured on the stress ROM (see BANANA.md in the repo): hidden symbol visibility,
# profile-guided optimisation from pgo/ (profiles recorded on the handheld, gcc 14.3 matching this toolchain).
LIBRETRO_BANANASNES_OPT = -O3 -fvisibility=hidden -fvisibility-inlines-hidden

define LIBRETRO_BANANASNES_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D)/libretro platform=unix CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" \
		OPT="$(LIBRETRO_BANANASNES_OPT)"
endef

define LIBRETRO_BANANASNES_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/libretro/snes9x_libretro.so $(TARGET_DIR)/usr/lib/libretro/bananasnes_libretro.so
endef

$(eval $(generic-package))

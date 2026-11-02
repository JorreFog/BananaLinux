################################################################################
#
# libretro-bananasnes
#
################################################################################

# BananaSNES: our Snes9x fork for the RG DS / RG DS Plus (https://github.com/JorreFog/BananaSNES, branch banana/main).
# Same licence as Snes9x (free distribution only); the core is installed as bananasnes_libretro.so next to snes9x's.
# Pin: BananaSNES banana/main at the verified core (a build from this commit == the binary measured on both handhelds; repo not pushed yet: needs the GitHub repo JorreFog/BananaSNES).
LIBRETRO_BANANASNES_VERSION = ee504367e2ea2f19474c4e7d6f79b2404418803f
LIBRETRO_BANANASNES_SITE = $(call github,JorreFog,BananaSNES,$(LIBRETRO_BANANASNES_VERSION))
LIBRETRO_BANANASNES_LICENSE = Snes9x (non-commercial)
LIBRETRO_BANANASNES_LICENSE_FILES = LICENSE

# The optimisation set measured on the handhelds (BANANA.md in the repo): profile-guided optimisation from pgo/ (recorded on the Plus with this
# toolchain's gcc 14.3 and the same sources; the profile names are relative to the source dir, -fprofile-prefix-path). Retrain pgo/ whenever a core
# source changes (tools/pgo-train.sh + pgo-build.sh); a stale profile only loses the speed-up, never changes the output.
LIBRETRO_BANANASNES_OPT = -O3 -fvisibility=hidden -fvisibility-inlines-hidden \
	-fprofile-use=$(@D)/pgo -fprofile-prefix-path=$(@D) -fprofile-partial-training -Wno-missing-profile -Wno-coverage-mismatch

define LIBRETRO_BANANASNES_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D)/libretro platform=unix CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" \
		OPT="$(LIBRETRO_BANANASNES_OPT)"
endef

define LIBRETRO_BANANASNES_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/libretro/snes9x_libretro.so $(TARGET_DIR)/usr/lib/libretro/bananasnes_libretro.so
endef

$(eval $(generic-package))

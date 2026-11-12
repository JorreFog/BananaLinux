################################################################################
#
# libretro-yabasanshiro
#
################################################################################

# YabaSanshiro's libretro port (libretro/yabause, the yabasanshiro branch's commit ROCKNIX pins): GLES 3 and the
# aarch64 dynarec. "rockpro64" is the Makefile's generic aarch64 Linux GLES platform.
# Ported from ROCKNIX's yabasanshiro-lr (packages/emulators/libretro/yabasanshiro-lr at 6cf94dba, GPL-2.0):
# the same commit, build options and patches.
# ROCKNIX adds -Wno-implicit-function-declaration; the two other -Wno- flags are for GCC 14, which makes them errors.
LIBRETRO_YABASANSHIRO_VERSION = 39535a6abcad5abf9f71c8b2a7975f005ee12ed6
LIBRETRO_YABASANSHIRO_SITE = https://github.com/libretro/yabause.git
LIBRETRO_YABASANSHIRO_SITE_METHOD = git
LIBRETRO_YABASANSHIRO_GIT_SUBMODULES = YES
LIBRETRO_YABASANSHIRO_LICENSE = GPL-2.0+
LIBRETRO_YABASANSHIRO_LICENSE_FILES = LICENSE
# Patches (beside this file, applied by Buildroot in order; each says where it comes from):
# - 0001-optimization.patch (ROCKNIX's 01-optimization.patch): the aarch64 dynarec (USE_AARCH64_DRC) for
#   rockpro64, without its Cortex-A72 flags

define LIBRETRO_YABASANSHIRO_BUILD_CMDS
	$(TARGET_MAKE_ENV) CFLAGS="$(TARGET_CFLAGS) -Wno-implicit-function-declaration -Wno-incompatible-pointer-types -Wno-int-conversion" $(MAKE) -C $(@D)/yabause/src/libretro platform=rockpro64 HAVE_NEON=0 FORCE_GLES=1 CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" AR="$(TARGET_AR)"
endef

define LIBRETRO_YABASANSHIRO_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/yabause/src/libretro/yabasanshiro_libretro.so $(TARGET_DIR)/usr/lib/libretro/yabasanshiro_libretro.so
endef

$(eval $(generic-package))

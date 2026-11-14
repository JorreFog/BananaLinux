################################################################################
#
# libretro-crocods
#
################################################################################

# CrocoDS, ROCKNIX's default Amstrad CPC core.
# Ported from ROCKNIX's crocods-lr (packages/emulators/libretro/crocods-lr at 6cf94dba, GPL-2.0):
# the same commit, build options and patches.
LIBRETRO_CROCODS_VERSION = a9c63b29443715ae2add392010fca4eae7f93e67
LIBRETRO_CROCODS_SITE = $(call github,libretro,libretro-crocods,$(LIBRETRO_CROCODS_VERSION))
LIBRETRO_CROCODS_LICENSE = MIT
LIBRETRO_CROCODS_LICENSE_FILES = LICENSE
# Patches (beside this file, applied by Buildroot in order; each says where it comes from):
# - 0001-keyboard.patch (ROCKNIX's 001-keyboard.patch): ROCKNIX's (from christianhaitian): the US keyboard layout
#   instead of the French one

define LIBRETRO_CROCODS_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) platform=unix CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" AR="$(TARGET_AR)"
endef

define LIBRETRO_CROCODS_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/crocods_libretro.so $(TARGET_DIR)/usr/lib/libretro/crocods_libretro.so
endef

$(eval $(generic-package))

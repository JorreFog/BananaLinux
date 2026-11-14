################################################################################
#
# libretro-genesis-plus-gx
#
################################################################################

# Genesis Plus GX: Master System, Game Gear, Mega Drive and Mega CD (the CD needs the BIOS files in /data/bios).
# Its licence allows free, non-commercial distribution only (the same terms as Snes9x's).
# Ported from ROCKNIX's genesis-plus-gx-lr (packages/emulators/libretro/genesis-plus-gx-lr at 6cf94dba, GPL-2.0):
# the same commit and build options.
LIBRETRO_GENESIS_PLUS_GX_VERSION = f27655b592ef43f317272d79883bcaa10d3ac8ad
LIBRETRO_GENESIS_PLUS_GX_SITE = $(call github,libretro,Genesis-Plus-GX,$(LIBRETRO_GENESIS_PLUS_GX_VERSION))
LIBRETRO_GENESIS_PLUS_GX_LICENSE = Genesis Plus GX (non-commercial)
LIBRETRO_GENESIS_PLUS_GX_LICENSE_FILES = LICENSE.txt

define LIBRETRO_GENESIS_PLUS_GX_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) -f Makefile.libretro platform=unix CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" AR="$(TARGET_AR)"
endef

define LIBRETRO_GENESIS_PLUS_GX_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/genesis_plus_gx_libretro.so $(TARGET_DIR)/usr/lib/libretro/genesis_plus_gx_libretro.so
endef

$(eval $(generic-package))

################################################################################
#
# libretro-fbneo
#
################################################################################

# FinalBurn Neo: arcade boards and the Neo Geo (MVS/AES). Its licence is non-commercial. libstdc++ linked dynamically,
# as ROCKNIX does (the Makefile's static one is dropped). No HAVE_NEON: ROCKNIX sets it only when its FPU flags name
# NEON (32-bit ARM), and it adds a 32-bit-only GCC flag.
# Ported from ROCKNIX's fbneo-lr (packages/emulators/libretro/fbneo-lr at 6cf94dba, GPL-2.0):
# the same commit and build options.
LIBRETRO_FBNEO_VERSION = e5d22930f772b8baff514ec57e5786ea50c1c72e
LIBRETRO_FBNEO_SITE = $(call github,libretro,FBNeo,$(LIBRETRO_FBNEO_VERSION))
LIBRETRO_FBNEO_LICENSE = FBNeo (non-commercial)
LIBRETRO_FBNEO_LICENSE_FILES = LICENSE.md
define LIBRETRO_FBNEO_LINK_FIX
	$(SED) 's|LDFLAGS += -static-libgcc -static-libstdc++|LDFLAGS += -static-libgcc|' $(@D)/src/burner/libretro/Makefile
endef
LIBRETRO_FBNEO_POST_PATCH_HOOKS += LIBRETRO_FBNEO_LINK_FIX

define LIBRETRO_FBNEO_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D)/src/burner/libretro USE_CYCLONE=0 profile=performance CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" AR="$(TARGET_AR)"
endef

define LIBRETRO_FBNEO_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/src/burner/libretro/fbneo_libretro.so $(TARGET_DIR)/usr/lib/libretro/fbneo_libretro.so
endef

$(eval $(generic-package))

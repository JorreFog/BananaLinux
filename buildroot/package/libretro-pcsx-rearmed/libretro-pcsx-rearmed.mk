################################################################################
#
# libretro-pcsx-rearmed
#
################################################################################

LIBRETRO_PCSX_REARMED_VERSION = c8816799b50388e61cfe237fe2cdbb7d8175f20a
LIBRETRO_PCSX_REARMED_SITE = https://github.com/libretro/pcsx_rearmed.git
LIBRETRO_PCSX_REARMED_SITE_METHOD = git
LIBRETRO_PCSX_REARMED_GIT_SUBMODULES = YES
LIBRETRO_PCSX_REARMED_LICENSE = GPL-2.0
LIBRETRO_PCSX_REARMED_LICENSE_FILES = COPYING

define LIBRETRO_PCSX_REARMED_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) -f Makefile.libretro platform=unix CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" ARCH=arm64 DYNAREC=ari64
endef

define LIBRETRO_PCSX_REARMED_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/pcsx_rearmed_libretro.so $(TARGET_DIR)/usr/lib/libretro/pcsx_rearmed_libretro.so
endef

$(eval $(generic-package))

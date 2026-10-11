################################################################################
#
# libretro-picodrive
#
################################################################################

LIBRETRO_PICODRIVE_VERSION = 1890c2932234c9d30f4cd3851d02228baae8f09e
LIBRETRO_PICODRIVE_SITE = https://github.com/libretro/picodrive.git
LIBRETRO_PICODRIVE_SITE_METHOD = git
LIBRETRO_PICODRIVE_GIT_SUBMODULES = YES
LIBRETRO_PICODRIVE_LICENSE = MAME-like (non-commercial)
LIBRETRO_PICODRIVE_LICENSE_FILES = COPYING

define LIBRETRO_PICODRIVE_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) -f Makefile.libretro platform=aarch64 CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" AS="$(TARGET_AS)"
endef

define LIBRETRO_PICODRIVE_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/picodrive_libretro.so $(TARGET_DIR)/usr/lib/libretro/picodrive_libretro.so
endef

$(eval $(generic-package))

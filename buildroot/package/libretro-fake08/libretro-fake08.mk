################################################################################
#
# libretro-fake08
#
################################################################################

# fake-08, a free PICO-8 player (its Lua fork z8lua is a submodule: fetched with git)
LIBRETRO_FAKE08_VERSION = 814991a2571ad3970e386cef48f3b148aa1c27b9
LIBRETRO_FAKE08_SITE = https://github.com/jtothebell/fake-08.git
LIBRETRO_FAKE08_SITE_METHOD = git
LIBRETRO_FAKE08_GIT_SUBMODULES = YES
LIBRETRO_FAKE08_LICENSE = MIT
LIBRETRO_FAKE08_LICENSE_FILES = LICENSE.MD

define LIBRETRO_FAKE08_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D)/platform/libretro platform=unix CC="$(TARGET_CC)" CXX="$(TARGET_CXX)"
endef

define LIBRETRO_FAKE08_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/platform/libretro/fake08_libretro.so $(TARGET_DIR)/usr/lib/libretro/fake08_libretro.so
endef

$(eval $(generic-package))

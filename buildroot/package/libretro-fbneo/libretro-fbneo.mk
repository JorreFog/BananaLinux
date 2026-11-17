################################################################################
#
# libretro-fbneo
#
################################################################################

LIBRETRO_FBNEO_VERSION = 77016b4dff6c76beba08d24ea1c61ed2750bc0a7
LIBRETRO_FBNEO_SITE = $(call github,libretro,FBNeo,$(LIBRETRO_FBNEO_VERSION))
# (free to use and share, not to sell; no asking for donations by a project that uses it)
LIBRETRO_FBNEO_LICENSE = FBNeo (non-commercial)
LIBRETRO_FBNEO_LICENSE_FILES = src/license.txt

define LIBRETRO_FBNEO_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D)/src/burner/libretro platform=unix CC="$(TARGET_CC)" CXX="$(TARGET_CXX)"
endef

# its license asks for its full text beside the program
define LIBRETRO_FBNEO_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/src/burner/libretro/fbneo_libretro.so $(TARGET_DIR)/usr/lib/libretro/fbneo_libretro.so
	$(INSTALL) -D -m 0644 $(@D)/src/license.txt $(TARGET_DIR)/usr/share/licenses/fbneo/license.txt
endef

$(eval $(generic-package))

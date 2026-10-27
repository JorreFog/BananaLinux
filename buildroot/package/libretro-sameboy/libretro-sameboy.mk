################################################################################
#
# libretro-sameboy
#
################################################################################

# ROCKNIX's sameboy-lr: upstream libretro/SameBoy at ROCKNIX's pinned commit.
LIBRETRO_SAMEBOY_VERSION = aa158a889a48b538a0302873704a34577c8eb67d
LIBRETRO_SAMEBOY_SITE = $(call github,libretro,SameBoy,$(LIBRETRO_SAMEBOY_VERSION))
LIBRETRO_SAMEBOY_LICENSE = MIT
LIBRETRO_SAMEBOY_LICENSE_FILES = LICENSE
LIBRETRO_SAMEBOY_DEPENDENCIES = host-util-linux

define LIBRETRO_SAMEBOY_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D)/libretro \
		CC="$(TARGET_CC)" BOOTROMS_DIR="$(@D)/BootROMs/prebuilt"
endef

define LIBRETRO_SAMEBOY_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/libretro/sameboy_libretro.so \
		$(TARGET_DIR)/usr/lib/libretro/sameboy_libretro.so
endef

$(eval $(generic-package))

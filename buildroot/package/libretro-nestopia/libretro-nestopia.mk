################################################################################
#
# libretro-nestopia
#
################################################################################

# ROCKNIX's nestopia-lr: upstream libretro/nestopia at ROCKNIX's pinned commit.
LIBRETRO_NESTOPIA_VERSION = b08b473f961e057216bc9e8c9d61054b1ab116cf
LIBRETRO_NESTOPIA_SITE = $(call github,libretro,nestopia,$(LIBRETRO_NESTOPIA_VERSION))
LIBRETRO_NESTOPIA_LICENSE = GPL-2.0+
LIBRETRO_NESTOPIA_LICENSE_FILES = COPYING

define LIBRETRO_NESTOPIA_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D)/libretro \
		CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" AR="$(TARGET_AR)"
endef

define LIBRETRO_NESTOPIA_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/libretro/nestopia_libretro.so \
		$(TARGET_DIR)/usr/lib/libretro/nestopia_libretro.so
endef

$(eval $(generic-package))

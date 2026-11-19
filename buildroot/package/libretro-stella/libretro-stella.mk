################################################################################
#
# libretro-stella
#
################################################################################

LIBRETRO_STELLA_VERSION = 5d206d713251ad7481ad2322742553105195968e
LIBRETRO_STELLA_SITE = $(call github,stella-emu,stella,$(LIBRETRO_STELLA_VERSION))
LIBRETRO_STELLA_LICENSE = GPL-2.0+
LIBRETRO_STELLA_LICENSE_FILES = License.txt

define LIBRETRO_STELLA_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D)/src/os/libretro platform=unix CC="$(TARGET_CC)" CXX="$(TARGET_CXX)"
endef

define LIBRETRO_STELLA_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/src/os/libretro/stella_libretro.so $(TARGET_DIR)/usr/lib/libretro/stella_libretro.so
endef

$(eval $(generic-package))

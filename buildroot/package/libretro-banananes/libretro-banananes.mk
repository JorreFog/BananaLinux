################################################################################
#
# libretro-banananes
#
################################################################################

# BananaNES: BananaOS's NES core, a fork of libretro-fceumm tuned for the RG DS / RG DS Plus (Cortex-A55).
# (local until it has a home on GitHub: the final step of the 2026-11-01 run points this at the pushed SHA)
LIBRETRO_BANANANES_VERSION = local
LIBRETRO_BANANANES_SITE = $(HOME)/banana-emu/BananaNES
LIBRETRO_BANANANES_SITE_METHOD = local
LIBRETRO_BANANANES_LICENSE = GPL-2.0
LIBRETRO_BANANANES_LICENSE_FILES = Copying

define LIBRETRO_BANANANES_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D)/banana BUILDROOT=1 CC="$(TARGET_CC)" \
		GIT_VERSION=$(LIBRETRO_BANANANES_VERSION)
endef

define LIBRETRO_BANANANES_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/build/target/banananes_libretro.so $(TARGET_DIR)/usr/lib/libretro/banananes_libretro.so
endef

$(eval $(generic-package))

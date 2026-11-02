################################################################################
#
# libretro-banananes
#
################################################################################

# BananaNES: BananaOS's NES core, a fork of libretro-fceumm tuned for the RG DS / RG DS Plus (Cortex-A55).
# from github.com/JorreFog/BananaNES (public since 2026-11-02)
LIBRETRO_BANANANES_VERSION = 314422eb4d52f054d4ffce39f3bfac87e8502bcc
LIBRETRO_BANANANES_SITE = $(call github,JorreFog,BananaNES,$(LIBRETRO_BANANANES_VERSION))
LIBRETRO_BANANANES_LICENSE = GPL-2.0
LIBRETRO_BANANANES_LICENSE_FILES = Copying

define LIBRETRO_BANANANES_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D)/banana BUILDROOT=1 CC="$(TARGET_CC)" \
		GIT_VERSION=$(LIBRETRO_BANANANES_VERSION)
endef

define LIBRETRO_BANANANES_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 `ls $(@D)/build/*/banananes_libretro.so | head -n 1` $(TARGET_DIR)/usr/lib/libretro/banananes_libretro.so
endef

$(eval $(generic-package))

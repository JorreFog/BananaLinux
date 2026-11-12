################################################################################
#
# wvkbd: the on-screen keyboard under run-wayland (the browser's)
#
################################################################################

WVKBD_VERSION = v0.21
WVKBD_SITE = $(call github,jjsullivan5196,wvkbd,$(WVKBD_VERSION))
WVKBD_LICENSE = GPL-3.0
WVKBD_LICENSE_FILES = LICENSE
WVKBD_DEPENDENCIES = host-pkgconf host-wayland wayland wayland-protocols cairo pango libxkbcommon

# its own Makefile, the one program (no man page: scdoc): the protocol sources made with the host's wayland-scanner
# (on the build's PATH); the compiler and flags in the environment, as the Makefile adds its own to them
define WVKBD_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(TARGET_CONFIGURE_OPTS) $(MAKE) -C $(@D) \
		PKG_CONFIG="$(PKG_CONFIG_HOST_BINARY)" wvkbd-mobintl
endef

define WVKBD_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/wvkbd-mobintl $(TARGET_DIR)/usr/bin/wvkbd-mobintl
endef

$(eval $(generic-package))

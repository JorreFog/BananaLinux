################################################################################
#
# banana-ui
#
################################################################################

BANANA_UI_VERSION = local
BANANA_UI_SITE = $(BR2_EXTERNAL_BANANA_PATH)/../ui
BANANA_UI_SITE_METHOD = local
BANANA_UI_LICENSE = MIT
BANANA_UI_LICENSE_FILES = ../LICENSE
BANANA_UI_DEPENDENCIES = freetype libdrm alsa-lib zlib host-pkgconf

define BANANA_UI_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -B -C $(@D) CXX="$(TARGET_CXX)" PKG_CONFIG="$(PKG_CONFIG_HOST_BINARY)" \
		CXXFLAGS="$(TARGET_CXXFLAGS) -O2" LDFLAGS="$(TARGET_LDFLAGS)" banana-ui banana-scrape
	$(TARGET_MAKE_ENV) $(MAKE) -B -C $(@D) CC="$(TARGET_CC)" CFLAGS="$(TARGET_CFLAGS)" LDFLAGS="$(TARGET_LDFLAGS)" banana-leaseshim.so
endef

define BANANA_UI_INSTALL_TARGET_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) DESTDIR=$(TARGET_DIR) install
endef

$(eval $(generic-package))

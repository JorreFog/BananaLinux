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
BANANA_UI_DEPENDENCIES = freetype libdrm alsa-lib zlib host-pkgconf libcurl openssl libssh2 libsmb2

define BANANA_UI_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -B -C $(@D) CXX="$(TARGET_CXX)" PKG_CONFIG="$(PKG_CONFIG_HOST_BINARY)" \
		CXXFLAGS="$(TARGET_CXXFLAGS) -O2" LDFLAGS="$(TARGET_LDFLAGS)" banana-ui banana-scrape banana-sync banana-bshare banana-fetch
	$(TARGET_MAKE_ENV) $(MAKE) -B -C $(@D) CC="$(TARGET_CC)" PKG_CONFIG="$(PKG_CONFIG_HOST_BINARY)" CFLAGS="$(TARGET_CFLAGS)" \
		LDFLAGS="$(TARGET_LDFLAGS)" banana-leaseshim.so libasound_module_pcm_banana_eq.so
	# Activity (store/apps/activity, #213): built here from its source, not the prebuilt binary beside it
	$(TARGET_CXX) $(TARGET_CXXFLAGS) -O2 -std=c++17 $(TARGET_LDFLAGS) -o $(@D)/banana-activity \
		$(BR2_EXTERNAL_BANANA_PATH)/../store/apps/activity/banana-activity.cpp
endef

define BANANA_UI_INSTALL_TARGET_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) DESTDIR=$(TARGET_DIR) install
	$(INSTALL) -D -m 0755 $(@D)/banana-activity $(TARGET_DIR)/usr/bin/banana-activity
	mkdir -p $(TARGET_DIR)/usr/share/banana/apps/activity
	touch $(TARGET_DIR)/usr/share/banana/apps/activity/Activity.app
endef

$(eval $(generic-package))

################################################################################
#
# bananad
#
################################################################################

BANANAD_VERSION = local
BANANAD_SITE = $(BR2_EXTERNAL_BANANA_PATH)/../system/bananad
BANANAD_SITE_METHOD = local
BANANAD_LICENSE = MIT

define BANANAD_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -B -C $(@D) CC="$(TARGET_CC)" CFLAGS="$(TARGET_CFLAGS)" LDFLAGS="$(TARGET_LDFLAGS)"
endef

define BANANAD_INSTALL_TARGET_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) DESTDIR=$(TARGET_DIR) install
endef

$(eval $(generic-package))

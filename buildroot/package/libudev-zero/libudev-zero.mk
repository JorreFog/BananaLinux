################################################################################
#
# libudev-zero
#
################################################################################

LIBUDEV_ZERO_VERSION = 1.0.5
LIBUDEV_ZERO_SITE = $(call github,illiliti,libudev-zero,$(LIBUDEV_ZERO_VERSION))
LIBUDEV_ZERO_LICENSE = ISC
LIBUDEV_ZERO_LICENSE_FILES = LICENSE
LIBUDEV_ZERO_INSTALL_STAGING = YES

define LIBUDEV_ZERO_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) CC="$(TARGET_CC)" AR="$(TARGET_AR)" CFLAGS="$(TARGET_CFLAGS)" PREFIX=/usr
endef

define LIBUDEV_ZERO_INSTALL_STAGING_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) DESTDIR=$(STAGING_DIR) PREFIX=/usr install
endef

define LIBUDEV_ZERO_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/libudev.so.1 $(TARGET_DIR)/usr/lib/libudev.so.1
	ln -sf libudev.so.1 $(TARGET_DIR)/usr/lib/libudev.so
endef

$(eval $(generic-package))

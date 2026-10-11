################################################################################
#
# rocknix-extra-firmware
#
################################################################################

# the commit (and archive hash) ROCKNIX's extra-firmware package pins
ROCKNIX_EXTRA_FIRMWARE_VERSION = 925dfb5efbe5d6c3ca654f81e3e1f367f8492777
ROCKNIX_EXTRA_FIRMWARE_SITE = https://github.com/ROCKNIX/extra-firmware/archive
ROCKNIX_EXTRA_FIRMWARE_SOURCE = $(ROCKNIX_EXTRA_FIRMWARE_VERSION).tar.gz
ROCKNIX_EXTRA_FIRMWARE_LICENSE = PROPRIETARY

define ROCKNIX_EXTRA_FIRMWARE_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/RK3566/aw88166_acf.bin $(TARGET_DIR)/lib/firmware/aw88166_acf.bin
endef

$(eval $(generic-package))

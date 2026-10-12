################################################################################
#
# rocknixds-bank
#
################################################################################

# ROCKNIXDS's release build (self-contained .NET for linux-arm64, invariant globalization: no ICU)
ROCKNIXDS_BANK_VERSION = 0.1.0
ROCKNIXDS_BANK_SOURCE = rocknixds-bank-$(ROCKNIXDS_BANK_VERSION)-aarch64.tar.gz
ROCKNIXDS_BANK_SITE = https://github.com/JorreFog/ROCKNIXDS/releases/download/bank-v$(ROCKNIXDS_BANK_VERSION)
ROCKNIXDS_BANK_LICENSE = MIT, GPL-3.0 (PKHeX.Core), MIT (.NET)
ROCKNIXDS_BANK_LICENSE_FILES = LICENSE THIRD_PARTY.md
ROCKNIXDS_BANK_STRIP_COMPONENTS = 1

# the app, and its tile's one entry with the pictures the menu shows for it
define ROCKNIXDS_BANK_INSTALL_TARGET_CMDS
	rm -rf $(TARGET_DIR)/usr/share/banana/apps/bank
	$(INSTALL) -d $(TARGET_DIR)/usr/share/banana/apps/bank/rocknixds-bank $(TARGET_DIR)/usr/share/banana/apps/bank/media
	cp -a $(@D)/. $(TARGET_DIR)/usr/share/banana/apps/bank/rocknixds-bank/
	rm -f $(TARGET_DIR)/usr/share/banana/apps/bank/rocknixds-bank/*.sh
	$(INSTALL) -m 0644 $(@D)/media/*.png $(TARGET_DIR)/usr/share/banana/apps/bank/media/
	$(INSTALL) -m 0644 $(ROCKNIXDS_BANK_PKGDIR)/gamelist.xml $(TARGET_DIR)/usr/share/banana/apps/bank/gamelist.xml
	touch "$(TARGET_DIR)/usr/share/banana/apps/bank/Bank & Trade.app"
endef

$(eval $(generic-package))

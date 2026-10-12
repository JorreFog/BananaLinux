################################################################################
#
# portmaster
#
################################################################################

PORTMASTER_VERSION = 2026.10.03-1041
PORTMASTER_SOURCE = PortMaster.zip
PORTMASTER_SITE = https://github.com/PortsMaster/PortMaster-GUI/releases/download/$(PORTMASTER_VERSION)
PORTMASTER_LICENSE = MIT
# the zip as released: unpacked on the card, not here
define PORTMASTER_EXTRACT_CMDS
endef

define PORTMASTER_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(PORTMASTER_DL_DIR)/$(PORTMASTER_SOURCE) $(TARGET_DIR)/usr/share/banana/portmaster/PortMaster.zip
	echo $(PORTMASTER_VERSION) > $(TARGET_DIR)/usr/share/banana/portmaster/version
endef

$(eval $(generic-package))

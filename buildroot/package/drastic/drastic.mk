################################################################################
#
# drastic
#
################################################################################

# The same archive ROCKNIX's drastic-sa package downloads
DRASTIC_VERSION = 1.0
DRASTIC_SOURCE = drastic.tar.gz
DRASTIC_SITE = https://github.com/ROCKNIX/packages/raw/main
DRASTIC_LICENSE = Proprietary (freeware)
DRASTIC_REDISTRIBUTE = NO

define DRASTIC_EXTRACT_CMDS
	$(TAR) -xzf $(DRASTIC_DL_DIR)/$(DRASTIC_SOURCE) -C $(@D)
endef

define DRASTIC_INSTALL_TARGET_CMDS
	mkdir -p $(TARGET_DIR)/usr/share/drastic
	cp -a $(@D)/drastic_aarch64/. $(TARGET_DIR)/usr/share/drastic/
endef

$(eval $(generic-package))

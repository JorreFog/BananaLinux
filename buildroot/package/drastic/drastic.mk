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

# ROCKNIX's settings and button map for the RG DS (board/rgds/drastic.cfg), with what ROCKNIXDS turns on for
# SuperDrastic: 2x internal resolution and 3D on its own thread
define DRASTIC_INSTALL_TARGET_CMDS
	mkdir -p $(TARGET_DIR)/usr/share/drastic/config
	cp -a $(@D)/drastic_aarch64/. $(TARGET_DIR)/usr/share/drastic/
	sed -e 's/^hires_3d = .*/hires_3d = 1/' -e 's/^threaded_3d = .*/threaded_3d = 1/' \
		$(BR2_EXTERNAL_BANANA_PATH)/board/rgds/drastic.cfg > $(TARGET_DIR)/usr/share/drastic/config/drastic.cfg
endef

$(eval $(generic-package))

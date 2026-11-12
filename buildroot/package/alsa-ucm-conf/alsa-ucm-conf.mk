################################################################################
#
# alsa-ucm-conf
#
################################################################################

ALSA_UCM_CONF_VERSION = 1.2.16.1
ALSA_UCM_CONF_SOURCE = alsa-ucm-conf-$(ALSA_UCM_CONF_VERSION).tar.bz2
ALSA_UCM_CONF_SITE = https://www.alsa-project.org/files/pub/lib
ALSA_UCM_CONF_LICENSE = BSD-3-Clause
ALSA_UCM_CONF_LICENSE_FILES = LICENSE

# data only: the ucm2 tree, read at run time by whoever opens the card (alsaucm, PipeWire's ACP)
define ALSA_UCM_CONF_INSTALL_TARGET_CMDS
	mkdir -p $(TARGET_DIR)/usr/share/alsa
	cp -r $(@D)/ucm2 $(TARGET_DIR)/usr/share/alsa/
	# the target tree drops empty directories, and UCM refuses to load without this one
	touch $(TARGET_DIR)/usr/share/alsa/ucm2/conf.virt.d/.dont_remove_this_dir
endef

$(eval $(generic-package))

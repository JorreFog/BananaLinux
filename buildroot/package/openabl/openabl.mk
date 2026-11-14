################################################################################
#
# openabl
#
################################################################################

# the release ROCKNIX's projects/ROCKNIX/packages/tools/openabl pins
OPENABL_VERSION = 1.2
OPENABL_SOURCE = openabl-v$(OPENABL_VERSION).tar.gz
OPENABL_SITE = https://github.com/OpenHandheldCollective/OpenABL/releases/download/v$(OPENABL_VERSION)
# (the release archive is binaries; its sources and licence are in the project's repository)
OPENABL_LICENSE = see https://github.com/OpenHandheldCollective/OpenABL
OPENABL_INSTALL_TARGET = NO
OPENABL_INSTALL_IMAGES = YES

# only the SM8550 image (the Thor, the Odin 2 family): board/thor/post-image.sh puts it on the boot partition with
# the flashing notes
define OPENABL_INSTALL_IMAGES_CMDS
	$(INSTALL) -D -m 0644 $$(find $(@D) -name 'abl_signed-SM8550.elf' | head -n 1) \
		$(BINARIES_DIR)/openabl/abl_signed-SM8550.elf
endef

$(eval $(generic-package))

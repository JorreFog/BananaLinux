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

ifeq ($(call qstrip,$(BR2_ROOTFS_POST_SCRIPT_ARGS)),thor)
# The AYN Thor's (ROCKNIX's SM8550 folder, the paths its device tree names): the audio DSP's firmware and its
# configuration (the battery manager's too: the charger and gauge are the ADSP's), the compute DSP, the AW88166
# speaker amplifiers' tuning, the sound card's topology, and the WCN7850's Wi-Fi files as ROCKNIX ships them (over
# linux-firmware's: the board file with the Thor's calibration)
define ROCKNIX_EXTRA_FIRMWARE_INSTALL_TARGET_CMDS
	cd $(@D)/SM8550 && for f in qcom/sm8550/ayn/thor/* qcom/sm8550/ayn/cdsp.mbn qcom/sm8550/ayn/cdsp_dtb.mbn \
			qcom/sm8550/AYN-Thor-tplg.bin ath12k/WCN7850/hw2.0/*; do \
		$(INSTALL) -D -m 0644 $$f $(TARGET_DIR)/lib/firmware/$$f || exit 1; \
	done
endef
else
define ROCKNIX_EXTRA_FIRMWARE_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/RK3566/aw88166_acf.bin $(TARGET_DIR)/lib/firmware/aw88166_acf.bin
endef
endif

$(eval $(generic-package))

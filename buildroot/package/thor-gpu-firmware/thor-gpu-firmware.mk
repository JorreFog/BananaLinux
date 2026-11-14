################################################################################
#
# thor-gpu-firmware
#
################################################################################

# The Adreno 740's three firmware files (the device tree and the msm driver ask for them by these names). Buildroot
# 2026.02's linux-firmware (20251011) is from before they were added; ROCKNIX's SM8550 image takes them from the
# release below (its packages/linux-firmware/kernel-firmware pin, devices/SM8550/config/kernel-firmware.dat). Only
# the three are unpacked from the release's archive. The rest of the Thor's firmware: linux-firmware's own options
# (the WCN7850's) and rocknix-extra-firmware (the DSPs', the sound topology, the Wi-Fi board file).
THOR_GPU_FIRMWARE_VERSION = 20260622
THOR_GPU_FIRMWARE_SOURCE = linux-firmware-$(THOR_GPU_FIRMWARE_VERSION).tar.xz
THOR_GPU_FIRMWARE_SITE = $(BR2_KERNEL_MIRROR)/linux/kernel/firmware
THOR_GPU_FIRMWARE_LICENSE = Proprietary (Qualcomm, redistributable)
THOR_GPU_FIRMWARE_LICENSE_FILES = LICENSES/LICENSE.qcom
THOR_GPU_FIRMWARE_REDISTRIBUTE = NO
THOR_GPU_FIRMWARE_FILES = qcom/a740_sqe.fw qcom/gmu_gen70200.bin qcom/sm8550/a740_zap.mbn

define THOR_GPU_FIRMWARE_EXTRACT_CMDS
	$(call suitable-extractor,$(THOR_GPU_FIRMWARE_SOURCE)) $(THOR_GPU_FIRMWARE_DL_DIR)/$(THOR_GPU_FIRMWARE_SOURCE) | \
		$(TAR) --strip-components=1 -C $(@D) $(TAR_OPTIONS) - \
		$(addprefix linux-firmware-$(THOR_GPU_FIRMWARE_VERSION)/,$(THOR_GPU_FIRMWARE_FILES) LICENSES/LICENSE.qcom)
endef

define THOR_GPU_FIRMWARE_INSTALL_TARGET_CMDS
	$(foreach f,$(THOR_GPU_FIRMWARE_FILES),$(INSTALL) -D -m 0644 $(@D)/$(f) $(TARGET_DIR)/lib/firmware/$(f)$(sep))
endef

$(eval $(generic-package))

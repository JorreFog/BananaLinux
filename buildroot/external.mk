include $(sort $(wildcard $(BR2_EXTERNAL_BANANA_PATH)/package/*/*.mk))

# SDL2 with its dummy video driver (Buildroot leaves it out): SuperDrastic runs DraStic with SDL_VIDEODRIVER=dummy and
# draws to the panels itself. Without the driver SDL_Init(VIDEO) fails and DraStic crashes a second in (its benchmark
# table indexed with garbage). The last --enable/--disable wins in configure.
SDL2_CONF_OPTS += --enable-video-dummy
# and its offscreen driver: GL apps made for one window over both panels draw there, and Banana's SDL patch
# (patches/sdl2) puts each frame on the panels (SDL_BANANA_PANELS)
SDL2_CONF_OPTS += --enable-video-offscreen
# and Vulkan surfaces (Buildroot builds SDL2 without): the Thor's Flycast and Vita3K draw through them
SDL2_CONF_OPTS += --enable-video-vulkan

# host-zstd's makefile turns lz4 support on when the build machine has lz4's headers, and zstd 1.5.7's fileio.c uses
# contentChecksum_t, a name lz4 1.11 removed: the host build fails on a PC with lz4 1.11 installed. Buildroot gives
# host-zstd no lz4 of its own, so it must not pick up the PC's (the zstd tool would link a library outside HOST_DIR).
HOST_ZSTD_OPTS += HAVE_LZ4=0

# The RG DS's drivers that mainline doesn't have, built into the kernel (no module loading before the menu): the
# generic DSI panel driver both panels use, and the ADC joypad driver for the sticks and buttons. Plus the two
# handhelds' device trees, from ROCKNIX (tools/sync-rocknix-hw.sh), with Banana's changes to them (dts-patches: the
# RG DS's panels at 60 Hz).
BANANA_RGDS = $(BR2_EXTERNAL_BANANA_PATH)/board/rgds
# the board this build is for: its post scripts' argument (rgds, rgds-plus, universal, thor). The RG DS's kernel
# drivers, device trees and patches go into an RG DS kernel only; the AYN Thor's come from board/thor (below).
BANANA_BOARD = $(call qstrip,$(BR2_ROOTFS_POST_SCRIPT_ARGS))
ifeq ($(BANANA_BOARD),thor)
BANANA_BOARD_DIR = $(BR2_EXTERNAL_BANANA_PATH)/board/thor
else
BANANA_BOARD_DIR = $(BANANA_RGDS)
endif

# The AYN Thor's CPU for every package: the Cortex-A710 (BR2_cortex_a710; the X3, A715 and A510 beside it are the same
# ARMv9.0) with SVE off. The Snapdragon 8 Gen 2 has SVE disabled, and GCC 14 with -mcpu=cortex-a710 vectorises plain
# loops with SVE at -O2 already (16 SVE instructions in a two-loop test file; none with +nosve), which would end the
# program with SIGILL on the handheld. ROCKNIX builds SM8550 with +nosve+nosve2 (devices/SM8550/options), Box64's SD8G2
# target too. Buildroot has no option for it: the compiler wrapper's -mcpu (BR_CPU) is GCC_TARGET_CPU, set from
# arch/arch.mk before this file, and read again when the wrapper and the packages build.
ifeq ($(BR2_cortex_a710)$(BANANA_BOARD),ythor)
GCC_TARGET_CPU := cortex-a710+nosve+nosve2
endif
# ROCKNIX's kernel patches, applied as ROCKNIX applies them (patch -p1, fuzz allowed): a few of them were written
# against a slightly different kernel and only fit with fuzz, which Buildroot's own patch step (-F0) refuses. Then the
# board's own (linux-patches-banana: kept apart, as the ROCKNIX sync replaces linux-patches; the AYN Thor's are from
# Banana Pocket, board/thor/linux-patches-banana/README.md)
define BANANA_LINUX_ROCKNIX_PATCHES
	for p in $(sort $(wildcard $(BANANA_BOARD_DIR)/linux-patches/*.patch)) \
	         $(sort $(wildcard $(BANANA_BOARD_DIR)/linux-patches-banana/*.patch)); do \
		echo "Applying $$(basename $$p)"; \
		patch -p1 -N --no-backup-if-mismatch -d $(LINUX_DIR) < $$p || exit 1; \
	done
endef
LINUX_POST_PATCH_HOOKS += BANANA_LINUX_ROCKNIX_PATCHES
define BANANA_LINUX_RGDS_DRIVERS
	cp $(BANANA_RGDS)/kernel/panel-generic-dsi.c $(LINUX_DIR)/drivers/gpu/drm/panel/
	grep -q panel-generic-dsi.o $(LINUX_DIR)/drivers/gpu/drm/panel/Makefile || \
		echo 'obj-$$(CONFIG_DRM_MIPI_DSI) += panel-generic-dsi.o' >> $(LINUX_DIR)/drivers/gpu/drm/panel/Makefile
	cp $(BANANA_RGDS)/kernel/rocknix-singleadc-joypad.c $(BANANA_RGDS)/kernel/rocknix-joypad.h $(LINUX_DIR)/drivers/input/joystick/
	grep -q rocknix-singleadc-joypad.o $(LINUX_DIR)/drivers/input/joystick/Makefile || \
		echo 'obj-$$(CONFIG_INPUT_JOYSTICK) += rocknix-singleadc-joypad.o' >> $(LINUX_DIR)/drivers/input/joystick/Makefile
	cp $(BANANA_RGDS)/dts/*.dts $(LINUX_DIR)/arch/arm64/boot/dts/rockchip/
	for p in $(sort $(wildcard $(BANANA_RGDS)/dts-patches/*.patch)); do \
		patch -p1 -N -d $(LINUX_DIR)/arch/arm64/boot/dts/rockchip < $$p || exit 1; \
	done
	for d in $(notdir $(basename $(wildcard $(BANANA_RGDS)/dts/*.dts))); do \
		grep -q "$$d.dtb" $(LINUX_DIR)/arch/arm64/boot/dts/rockchip/Makefile || \
			echo "dtb-\$$(CONFIG_ARCH_ROCKCHIP) += $$d.dtb" >> $(LINUX_DIR)/arch/arm64/boot/dts/rockchip/Makefile; \
	done
endef
ifneq ($(BANANA_BOARD),thor)
LINUX_POST_PATCH_HOOKS += BANANA_LINUX_RGDS_DRIVERS
endif

# The AYN Thor (SM8550): ROCKNIX's device trees (qcs8550-ayn-thor.dts and its common .dtsi, tools/sync-rocknix-sm8550.sh)
# into the kernel's qcom directory, with Banana's changes to them (board/thor/dts-patches). Its drivers (the RSInput
# gamepad, both AMOLED panels, the stick LEDs, the haptics) come in ROCKNIX's patches above.
BANANA_THOR = $(BR2_EXTERNAL_BANANA_PATH)/board/thor
define BANANA_LINUX_THOR_DTS
	cp $(BANANA_THOR)/dts/*.dts $(BANANA_THOR)/dts/*.dtsi $(LINUX_DIR)/arch/arm64/boot/dts/qcom/
	for p in $(sort $(wildcard $(BANANA_THOR)/dts-patches/*.patch)); do \
		patch -p1 -N -d $(LINUX_DIR)/arch/arm64/boot/dts/qcom < $$p || exit 1; \
	done
	for d in $(notdir $(basename $(wildcard $(BANANA_THOR)/dts/*.dts))); do \
		grep -q "$$d.dtb" $(LINUX_DIR)/arch/arm64/boot/dts/qcom/Makefile || \
			echo "dtb-\$$(CONFIG_ARCH_QCOM) += $$d.dtb" >> $(LINUX_DIR)/arch/arm64/boot/dts/qcom/Makefile; \
	done
endef
ifeq ($(BANANA_BOARD),thor)
LINUX_POST_PATCH_HOOKS += BANANA_LINUX_THOR_DTS
endif

# Turnip's render-pass autotuner corrected by measured pass times (ROCKNIX's drirc file, 01-rocknix-turnip.conf; the
# option comes with patches/mesa3d, ROCKNIX's autotune patches): TU_AUTOTUNE_FLAGS in a game's environment still wins
define BANANA_MESA3D_THOR_DRIRC
	$(INSTALL) -D -m 0644 $(BANANA_THOR)/turnip-drirc.conf $(TARGET_DIR)/usr/share/drirc.d/01-banana-turnip.conf
endef
ifeq ($(BANANA_BOARD)$(BR2_PACKAGE_MESA3D_VULKAN_DRIVER_FREEDRENO),thory)
MESA3D_POST_INSTALL_TARGET_HOOKS += BANANA_MESA3D_THOR_DRIRC
endif
# PipeWire's quantum floor for the Thor's DSP (board/thor/pipewire/50-banana-thor.conf says why)
define BANANA_PIPEWIRE_THOR_CONF
	$(INSTALL) -D -m 0644 $(BANANA_THOR)/pipewire/50-banana-thor.conf \
		$(TARGET_DIR)/etc/pipewire/pipewire.conf.d/50-banana-thor.conf
endef
ifeq ($(BANANA_BOARD)$(BR2_PACKAGE_PIPEWIRE),thory)
PIPEWIRE_POST_INSTALL_TARGET_HOOKS += BANANA_PIPEWIRE_THOR_CONF
endif

# The RTL8821CS's Bluetooth firmware (btrtl, through hci_uart's H5): rtl_bt/rtl8821cs_fw.bin and the config it needs
# for the UART (1.5 Mbaud, flow control), which linux-firmware ships as a link to rtl8761b_config.bin (WHENCE: Link
# rtl_bt/rtl8821cs_config.bin -> rtl8761b_config.bin; Buildroot makes the link once its target is installed).
# Buildroot's Realtek BT options would bring every 87xx/88xx file instead of these two.
# The AYN Thor's from linux-firmware beyond Buildroot's options (ROCKNIX's devices/SM8550/config/kernel-firmware.dat):
# the WCN7850's Bluetooth half. (The Adreno 740's own three files aren't in this linux-firmware release at all:
# package/thor-gpu-firmware takes them from the one ROCKNIX uses.)
ifeq ($(BR2_PACKAGE_LINUX_FIRMWARE)$(BANANA_BOARD),ythor)
LINUX_FIRMWARE_FILES += qca/hmtbtfw20.tlv qca/hmtnv20.*
LINUX_FIRMWARE_ALL_LICENSE_FILES += LICENSE.qcom qcom/NOTICE.txt
endif

ifeq ($(BR2_PACKAGE_BLUEZ5_UTILS)$(BR2_PACKAGE_LINUX_FIRMWARE)$(filter thor,$(BANANA_BOARD)),yy)
LINUX_FIRMWARE_FILES += rtl_bt/rtl8821cs_fw.bin rtl_bt/rtl8761b_config.bin
LINUX_FIRMWARE_ALL_LICENSE_FILES += LICENCE.rtlwifi_firmware.txt
endif

# bluez-alsa's codecs beyond Buildroot's options (aptX and aptX HD come with BR2_PACKAGE_LIBOPENAPTX through
# bluez-alsa.mk): LDAC (Sony's encoder, package/ldacbt) and FastStream (SBC-based, no library). Which ones a
# connection may use is the bluetooth script's (Settings > Network > Bluetooth audio: bluealsa -c ...).
ifeq ($(BR2_PACKAGE_BLUEZ_ALSA)$(BR2_PACKAGE_LDACBT),yy)
BLUEZ_ALSA_DEPENDENCIES += ldacbt
BLUEZ_ALSA_CONF_OPTS += --enable-ldac
# Buildroot reads this file after bluez-alsa.mk has been evaluated, so the line above no longer orders the build (the
# CONF_OPTS still apply: they're read when configure runs): ldacbt's staging install made a prerequisite of
# bluez-alsa's configure step directly, or configure didn't find ldacBT-abr.pc
$(BLUEZ_ALSA_TARGET_CONFIGURE): | $(LDACBT_TARGET_INSTALL_STAGING)
endif
ifeq ($(BR2_PACKAGE_BLUEZ_ALSA),y)
BLUEZ_ALSA_CONF_OPTS += --enable-faststream
endif

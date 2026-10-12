include $(sort $(wildcard $(BR2_EXTERNAL_BANANA_PATH)/package/*/*.mk))

# SDL2 with its dummy video driver (Buildroot leaves it out): SuperDrastic runs DraStic with SDL_VIDEODRIVER=dummy and
# draws to the panels itself. Without the driver SDL_Init(VIDEO) fails and DraStic crashes a second in (its benchmark
# table indexed with garbage). The last --enable/--disable wins in configure.
SDL2_CONF_OPTS += --enable-video-dummy
# and its offscreen driver: GL apps made for one window over both panels draw there, and Banana's SDL patch
# (patches/sdl2) puts each frame on the panels (SDL_BANANA_PANELS)
SDL2_CONF_OPTS += --enable-video-offscreen

# The RG DS's drivers that mainline doesn't have, built into the kernel (no module loading before the menu): the
# generic DSI panel driver both panels use, and the ADC joypad driver for the sticks and buttons. Plus the two
# handhelds' device trees, from ROCKNIX (tools/sync-rocknix-hw.sh), with Banana's changes to them (dts-patches: the
# RG DS's panels at 60 Hz).
BANANA_RGDS = $(BR2_EXTERNAL_BANANA_PATH)/board/rgds
# ROCKNIX's kernel patches, applied as ROCKNIX applies them (patch -p1, fuzz allowed): a few of them were written
# against a slightly different kernel and only fit with fuzz, which Buildroot's own patch step (-F0) refuses
define BANANA_LINUX_ROCKNIX_PATCHES
	for p in $(sort $(wildcard $(BANANA_RGDS)/linux-patches/*.patch)); do \
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
LINUX_POST_PATCH_HOOKS += BANANA_LINUX_RGDS_DRIVERS

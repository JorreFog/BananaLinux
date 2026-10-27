# Banana: a light Linux for the Anbernic RG DS and RG DS Plus.
#
#   make rgds          the RG DS's SD card image        -> output/rgds/images/banana-rgds.img.gz
#   make rgds-plus     the RG DS Plus's SD card image   -> output/rgds-plus/images/banana-rgds-plus.img.gz
#   make ui            banana-ui for this computer (headless renders, no panels needed)
#   make test          renders the menu headless from a test library and checks it (needs ImageMagick)
#   make menuconfig-rgds / linux-menuconfig-rgds ...   any Buildroot target, per board
#
# The first image build downloads Buildroot (below) and everything it builds: expect an hour or more.
BUILDROOT_VERSION = 2026.02.3
BUILDROOT_DIR ?= $(CURDIR)/.buildroot
OUT ?= $(CURDIR)/output
BOARDS = rgds rgds-plus

$(BUILDROOT_DIR)/Makefile:
	git clone --depth 1 --branch $(BUILDROOT_VERSION) https://github.com/buildroot/buildroot $(BUILDROOT_DIR)

define board
$(OUT)/$(1)/.config: $(BUILDROOT_DIR)/Makefile buildroot/configs/banana_$(subst -,_,$(1))_defconfig
	$(MAKE) -C $(BUILDROOT_DIR) O=$(OUT)/$(1) BR2_EXTERNAL=$(CURDIR)/buildroot banana_$(subst -,_,$(1))_defconfig

$(1): $(OUT)/$(1)/.config
	$(MAKE) -C $(OUT)/$(1)

%-$(1): $(OUT)/$(1)/.config
	$(MAKE) -C $(OUT)/$(1) $$*
endef
$(foreach b,$(BOARDS),$(eval $(call board,$(b))))

ui:
	$(MAKE) -C ui host

test: ui
	ui/tests/run.sh

clean:
	rm -rf $(OUT)
	$(MAKE) -C ui clean

.PHONY: $(BOARDS) ui test clean

# Banana: a light Linux for the Retroid Pocket Classic (Qualcomm SM4450).
#
#   make retroid-pocket-classic   the SD card image   -> output/retroid-pocket-classic/images/banana-retroid-pocket-classic.img.gz
#   make ui                       banana-ui for this computer (headless renders, no panel needed)
#   make test                     renders the menu headless from a test library and checks it (needs ImageMagick)
#   make menuconfig-retroid-pocket-classic / linux-menuconfig-retroid-pocket-classic ...   any Buildroot target
#
# The first image build downloads Buildroot (below) and everything it builds: expect an hour or more.
BUILDROOT_VERSION = 2026.02.3
BUILDROOT_DIR ?= $(CURDIR)/.buildroot
OUT ?= $(CURDIR)/output
BOARDS = retroid-pocket-classic

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

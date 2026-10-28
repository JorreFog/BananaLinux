# Banana: a light Linux for the Retroid Pocket Classic (Qualcomm SM4450).
#
#   make retroid-pocket-classic   the SD card image   -> output/retroid-pocket-classic/images/banana-retroid-pocket-classic.img.gz
#   make ui                       banana-ui for this computer (headless renders, no panel needed)
#   make test                     renders the menu headless from a test library and checks it (needs ImageMagick)
#   make bench                    a host measurement baseline (scan/start-up/frame cost) at both canvas sizes
#   make local-rebuild            rebuild the in-tree packages (banana-ui, bananad) before a board build
#   make menuconfig-retroid-pocket-classic / linux-menuconfig-retroid-pocket-classic ...   any Buildroot target
#
# The first image build downloads Buildroot (below) and everything it builds: expect an hour or more.
#
# ui/ and system/bananad/ are Buildroot 'local' packages (built from this tree). Buildroot does not notice a source
# change in them, so a plain `make retroid-pocket-classic` after editing them ships the old binary. The board
# target below rebuilds them when any source in ui/src, ui/rnds or system/bananad is newer than the built binary,
# so a source edit is never shipped in the old binary. `make local-rebuild` does the same by hand.
BUILDROOT_VERSION = 2026.02.3
BUILDROOT_DIR ?= $(CURDIR)/.buildroot
OUT ?= $(CURDIR)/output
BOARDS = retroid-pocket-classic
LOCAL_PKGS = banana-ui-rebuild bananad-rebuild
# the in-tree packages' sources: the board target compares these against the built binaries to decide whether to
# rebuild. `find` is run once, at parse time, so the list is current.
LOCAL_SRC = $(shell find ui/src ui/rnds system/bananad -type f \( -name '*.cpp' -o -name '*.c' -o -name '*.h' -o -name '*.hpp' \) 2>/dev/null)

$(BUILDROOT_DIR)/Makefile:
	git clone --depth 1 --branch $(BUILDROOT_VERSION) https://github.com/buildroot/buildroot $(BUILDROOT_DIR)

define board
$(OUT)/$(1)/.config: $(BUILDROOT_DIR)/Makefile buildroot/configs/banana_$(subst -,_,$(1))_defconfig
	$(MAKE) -C $(BUILDROOT_DIR) O=$(OUT)/$(1) BR2_EXTERNAL=$(CURDIR)/buildroot banana_$(subst -,_,$(1))_defconfig

# a stamp that is only rebuilt when an in-tree source changes. Its recipe rebuilds the packages (banana-ui,
# bananad) and touches itself, so the board target below only rebuilds them when something actually changed. On a
# tree where the image was never built the binaries do not exist yet and the board target builds them for the first
# time (the recipe runs regardless, since the stamp has no prerequisites there).
ifneq ($(wildcard $(OUT)/$(1)/target/usr/bin/banana-ui),)
$(OUT)/$(1)/.local-stamp: $(LOCAL_SRC)
	$(MAKE) -C $(OUT)/$(1) $(LOCAL_PKGS)
	@mkdir -p $(OUT)/$(1)/
	@touch $(OUT)/$(1)/.local-stamp
else
$(OUT)/$(1)/.local-stamp:
	@mkdir -p $(OUT)/$(1)/
	@touch $(OUT)/$(1)/.local-stamp
endif

# the image: the stamp rebuilds the in-tree packages when their sources are newer than the built binaries, so a
# board build never ships a stale banana-ui/bananad (Buildroot's 'local' packages are not tracked by its own make).
$(1): $(OUT)/$(1)/.config $(OUT)/$(1)/.local-stamp
	$(MAKE) -C $(OUT)/$(1)

%-$(1): $(OUT)/$(1)/.config
	$(MAKE) -C $(OUT)/$(1) $$*
endef
$(foreach b,$(BOARDS),$(eval $(call board,$(b))))

ui:
	$(MAKE) -C ui host

test: ui
	ui/tests/run.sh

# a host-only measurement baseline (the library scan, the menu's start-up and the frame cost) at both canvas
# sizes; the numbers tell a regression from a change. See ui/tests/bench.sh.
bench: ui
	ui/tests/bench.sh

# The in-tree packages are 'local': rebuild them so a board build picks up edits to ui/ or system/bananad/
# (they must have been built once already; `make retroid-pocket-classic` does that on a fresh tree).
local-rebuild:
	$(MAKE) -C $(OUT)/retroid-pocket-classic $(LOCAL_PKGS)

# the image's banana-ui must match a fresh host build. The CI's `checks` job asserts this hash; on a tree where the
# image was never built it asks you to run `make retroid-pocket-classic` first.
check-local-build: ui
	@out=$$(cd $(OUT)/retroid-pocket-classic && find target/usr/bin/banana-ui -type f 2>/dev/null); \
	if [ -z "$$out" ]; then echo "no image built yet: run make retroid-pocket-classic first"; exit 1; fi; \
	host=$$(sha256sum $(CURDIR)/banana-ui | cut -d' ' -f1); \
	target=$$(sha256sum $$out | cut -d' ' -f1); \
	if [ "$$host" != "$$target" ]; then echo "stale image: host banana-ui $$host != image $$target"; exit 1; fi; \
	echo "local build matches the image ($$host)"

clean:
	rm -rf $(OUT)
	$(MAKE) -C ui clean

.PHONY: $(BOARDS) ui test bench local-rebuild check-local-build clean

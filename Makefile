# Banana: a light Linux for the Anbernic RG DS and RG DS Plus, and (in bring-up) the AYN Thor.
#
#   make rgds          the RG DS's SD card image        -> output/rgds/images/banana-rgds.img.gz
#   make rgds-plus     the RG DS Plus's SD card image   -> output/rgds-plus/images/banana-rgds-plus.img.gz
#   make universal     one card for both                -> output/universal/images/banana-universal.img.gz
#   make thor          the AYN Thor's (SM8550; in bring-up: docs/BUILD-THOR-IMAGE.md, docs/THOR-OS.md) -> output/thor/images/banana-thor.img.gz
#   make ui            banana-ui for this computer (headless renders, no panels needed), and its helpers when their
#                      libraries are there (banana-fetch: libcurl, libssh2, OpenSSL; libsmb2 too, or it has no SMB)
#   make test          renders the menu headless from a test library and checks it (needs ImageMagick), then the
#                      helpers' own checks (banana-fetch's: archives, the session, the runner; 7zz for 7z and RAR)
#   make fuzz          the readers of the card's files fuzzed (needs clang; FUZZ_SECONDS each, 60 by default)
#   make theme-studio  the layout-theme editor's tests (the expression port, the preview server, the schema)
#   make theme-studio-desktop  the PySide6 designer's smoke test
#   make menuconfig-rgds / linux-menuconfig-rgds ...   any Buildroot target, per board
#
# The first image build downloads Buildroot (below) and everything it builds: expect an hour or more.
BUILDROOT_VERSION = 2026.02.3
BUILDROOT_DIR ?= $(CURDIR)/.buildroot
ROCKNIXDS_STORE ?=	# (the Store's test: a rocknixds-store checkout with BananaOS's patches)
OUT ?= $(CURDIR)/output
BOARDS = rgds rgds-plus universal thor

$(BUILDROOT_DIR)/Makefile:
	git clone --depth 1 --branch $(BUILDROOT_VERSION) https://github.com/buildroot/buildroot $(BUILDROOT_DIR)

# Buildroot itself is downloaded, not vendored, so the few changes Banana makes to Buildroot (rather than to a
# package's sources: those are in buildroot/patches/) sit in buildroot/buildroot-patches/ and are applied here: Mesa's
# Turnip (Vulkan for the AYN Thor's Adreno) and a newer Mesa, as Banana Pocket has them, and libxkbcommon's xkbregistry
# as an option (Wine's Wayland driver needs it). Idempotent: a patch already in place is left alone. (A board's
# packages built before a new patch here: make <pkg>-dirclean-<board> first.)
BUILDROOT_PATCHES = $(wildcard $(CURDIR)/buildroot/buildroot-patches/*.patch)
$(BUILDROOT_DIR)/.banana-patched: $(BUILDROOT_DIR)/Makefile $(BUILDROOT_PATCHES)
	@for p in $(BUILDROOT_PATCHES); do \
		if git -C $(BUILDROOT_DIR) apply --reverse --check "$$p" 2>/dev/null; then \
			echo "buildroot patch already applied: $$(basename $$p)"; \
		else \
			echo "applying buildroot patch: $$(basename $$p)"; \
			git -C $(BUILDROOT_DIR) apply "$$p" || exit 1; \
		fi; \
	done
	@touch $@
buildroot-patched: $(BUILDROOT_DIR)/.banana-patched

define board
$(OUT)/$(1)/.config: $(BUILDROOT_DIR)/.banana-patched buildroot/configs/banana_$(subst -,_,$(1))_defconfig
	$(MAKE) -C $(BUILDROOT_DIR) O=$(OUT)/$(1) BR2_EXTERNAL=$(CURDIR)/buildroot banana_$(subst -,_,$(1))_defconfig

$(1): $(OUT)/$(1)/.config
	$(MAKE) -C $(OUT)/$(1)

%-$(1): $(OUT)/$(1)/.config
	$(MAKE) -C $(OUT)/$(1) $$*
endef
$(foreach b,$(BOARDS),$(eval $(call board,$(b))))

ui:
	$(MAKE) -C ui host
	@if pkg-config --exists libcurl libcrypto; then $(MAKE) -C ui banana-bshare; \
	else echo "banana-bshare not built (libcurl's and OpenSSL's dev packages): Bshare's tests are skipped"; fi
	@if pkg-config --exists libcurl libssh2 libssl libcrypto zlib; then \
		$(MAKE) -C ui banana-fetch SMB=$$(pkg-config --exists libsmb2 && echo 1 || echo 0); \
	else echo "banana-fetch not built (libcurl's, libssh2's and OpenSSL's dev packages): Files' servers' tests are skipped"; fi

test: ui
	ui/tests/run.sh
	@if [ -x ui/banana-bshare ]; then sh ui/tests/bshare-test.sh; fi
	@if [ -x ui/banana-fetch ]; then sh ui/tests/fetch-test.sh; fi
	@if [ -n "$(ROCKNIXDS_STORE)" ]; then python3 store/test-banana.py "$(ROCKNIXDS_STORE)"; else echo "store test skipped (set ROCKNIXDS_STORE to a rocknixds-store checkout)"; fi

# ui/fuzz: themes, expressions, zips, DS ROM banners and Banana's own files on the card, under libFuzzer, ASan and UBSan
fuzz:
	sh ui/fuzz/run.sh

# tools/theme-studio: the layout-theme editor's tests (expr.js vs ui/src/expr.cpp, the preview server, the schema)
theme-studio: ui
	sh tools/theme-studio/tests/run.sh

# the PySide6 designer's headless smoke test (needs PySide6)
theme-studio-desktop:
	python3 tools/theme-studio-desktop/tests/smoke.py

clean:
	rm -rf $(OUT)
	$(MAKE) -C ui clean

.PHONY: $(BOARDS) ui test fuzz theme-studio theme-studio-desktop clean buildroot-patched

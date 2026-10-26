################################################################################
#
# pocket-native -- a Nintendo 3DS game's code, recompiled for the handheld by
# a PC and run by the parts of an emulator it still needs.
#
# The fork is pinned by commit and the patches beside this file are Banana's
# own on top of it: both panels on the RG DS Plus, its buttons numbered as its
# DTB says they are, the touch screen read, and the renderer's caches kept from
# filling a 1 GB handheld. They are sent upstream; when they land, drop the
# patches and move POCKET_NATIVE_VERSION to the commit that has them.
#
# The image carries the runtime only. A game's recompiled library (title.so)
# and its ROM are the player's own, built on a PC by pocket-native's kit and
# copied to the card -- see docs/3DS.md.
#
################################################################################

POCKET_NATIVE_VERSION = fc6a95dd0c5e4b8ada904466547c0b76948ba260
POCKET_NATIVE_SITE = $(call github,whovian64,pocket-native,$(POCKET_NATIVE_VERSION))
POCKET_NATIVE_LICENSE = MIT
POCKET_NATIVE_LICENSE_FILES = LICENSE
# the frontend crate; the checkout is a Cargo workspace (engine/, recompiler/,
# frontend/, android/), and the subset it needs is built from here
POCKET_NATIVE_SUBDIR = frontend
# Mesa (EGL on GBM for the panels and the 3D), libdrm; pkg-config for EGL
POCKET_NATIVE_DEPENDENCIES = host-pkgconf libdrm mesa3d-panfrost

# the per-game tuning the frontend reads, by title id: small toml files, no
# game data (a game nobody has profiled simply gets the defaults)
define POCKET_NATIVE_INSTALL_PROFILES
	$(INSTALL) -d $(TARGET_DIR)/usr/share/banana/pocket-native
	cp -r $(@D)/titles $(TARGET_DIR)/usr/share/banana/pocket-native/titles
endef
POCKET_NATIVE_POST_INSTALL_TARGET_HOOKS += POCKET_NATIVE_INSTALL_PROFILES

$(eval $(cargo-package))

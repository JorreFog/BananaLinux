################################################################################
#
# bananads: BananaDS, the DS emulator on melonDS's core (docs/EMULATORS.md). It lives in the SuperDrastic
# repository under bananads/ (CMake); its build fetches melonDS at configure time, which Buildroot's offline build
# step can't, so melonDS is a second download here (the commit BananaDS pins, BANANADS_MELONDS_COMMIT in its
# CMakeLists.txt) and BananaDS's patches to it (bananads/patches/melonds) are applied by Buildroot's patch step,
# not by BananaDS's CMake (its git apply would skip every file inside another git checkout, as the CI's is).
#
################################################################################

BANANADS_VERSION = 2f65e3e7e3fd79c55b3783bc15e33ebbb6cc230b
BANANADS_SITE = https://github.com/JorreFog/SuperDrastic.git
BANANADS_SITE_METHOD = git
BANANADS_SUBDIR = bananads
BANANADS_LICENSE = GPL-3.0+
BANANADS_LICENSE_FILES = bananads/LICENSE
# a build folder of its own (bananads/buildroot-build): melonDS's teakra refuses to configure in its source folder
BANANADS_SUPPORTS_IN_SOURCE_BUILD = NO
# libdrm (the panels), ALSA (sound and the microphone), zlib (melonDS)
BANANADS_DEPENDENCIES = libdrm alsa-lib zlib host-pkgconf

BANANADS_MELONDS_VERSION = 906e9ebb27da8c6a715cd7abab4abfe8a8d29427
BANANADS_EXTRA_DOWNLOADS = https://github.com/melonDS-emu/melonDS/archive/$(BANANADS_MELONDS_VERSION).tar.gz

# melonDS beside BananaDS's sources, then BananaDS's patches to it, in order
define BANANADS_MELONDS_EXTRACT
	mkdir -p $(@D)/melonds
	$(call suitable-extractor,$(BANANADS_MELONDS_VERSION).tar.gz) \
		$(BANANADS_DL_DIR)/$(BANANADS_MELONDS_VERSION).tar.gz | \
		$(TAR) --strip-components=1 -C $(@D)/melonds $(TAR_OPTIONS) -
endef
BANANADS_POST_EXTRACT_HOOKS += BANANADS_MELONDS_EXTRACT

define BANANADS_MELONDS_PATCH
	$(APPLY_PATCHES) $(@D)/melonds $(@D)/bananads/patches/melonds \*.patch
endef
BANANADS_POST_PATCH_HOOKS += BANANADS_MELONDS_PATCH

# The handhelds' build (BananaDS's docs/BANANALINUX.md, B1): melonDS's JIT, no SDL window, ALSA, the KMS frontend,
# no LAN play (libenet), tuned for the Cortex-A55 with link-time optimization
BANANADS_CONF_OPTS = \
	-DBANANADS_MELONDS_DIR=$(@D)/melonds \
	-DBANANADS_MELONDS_PATCHES=OFF \
	-DBANANADS_JIT=melonds \
	-DBANANADS_SDL=OFF \
	-DBANANADS_ALSA=ON \
	-DBANANADS_KMS=ON \
	-DBANANADS_LAN=OFF \
	-DBANANADS_CPU=cortex-a55 \
	-DBANANADS_LTO=ON

# link-time optimization needs an archiver that indexes LTO objects: the external toolchain's own gcc-ar and
# gcc-ranlib (host/bin's names for them go through the compiler wrapper)
ifeq ($(BR2_TOOLCHAIN_EXTERNAL),y)
BANANADS_CONF_OPTS += \
	-DCMAKE_AR=$(TOOLCHAIN_EXTERNAL_CROSS)gcc-ar \
	-DCMAKE_RANLIB=$(TOOLCHAIN_EXTERNAL_CROSS)gcc-ranlib
endif

# only the emulator (not the host tests and benchmarks)
BANANADS_MAKE_OPTS = bananads

define BANANADS_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(BANANADS_BUILDDIR)/bananads $(TARGET_DIR)/usr/bin/bananads
endef

$(eval $(cmake-package))

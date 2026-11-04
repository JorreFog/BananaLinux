################################################################################
#
# bananafront
#
################################################################################

# from github.com/JorreFog/BananaFront (public since 2026-11-02)
# (the Commodore 64 / ZX Spectrum keyboard support and the on-screen keyboard are on BananaFront's
#  feat/c64-zxspectrum branch, on top of e6b8e47: bump BANANAFRONT_VERSION to its commit when it lands upstream)
# the BananaFront commit 0.5.1.1 was tested with: e6b8e47 (per-system Buttons + Swap A/B and X/Y + stick as d-pad, #151/#142; F hotkeys always on; on 654b40e: no shader = sharp CPU-nearest scaling, fast-forward with a shader at full speed; on 3f9a552: archives zlib cannot open unpacked with 7zz, KAN-146; on top of 47c7b85, F menu only as a tap, and e6cfc20, the GPU clock floor while a shader runs, KAN-144))
BANANAFRONT_VERSION = e6b8e47d04d3ef6fc3f769c3b988bf3b70055f3b
BANANAFRONT_SITE = $(call github,JorreFog,BananaFront,$(BANANAFRONT_VERSION))
BANANAFRONT_LICENSE = GPL-3.0+, MIT (libretro.h, rcheevos, bcomp), public domain/MIT (stb)
BANANAFRONT_LICENSE_FILES = LICENSE src/third_party/rcheevos/LICENSE
BANANAFRONT_DEPENDENCIES = libdrm alsa-lib zlib

define BANANAFRONT_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) device OUT=$(@D)/build/target \
		CC="$(TARGET_CC)" AR="$(TARGET_AR)" \
		A55_CFLAGS="$(TARGET_CFLAGS) -mcpu=cortex-a55 -O2" A55_LDFLAGS="$(TARGET_LDFLAGS)" \
		A55_DRM_CFLAGS="-I$(STAGING_DIR)/usr/include/libdrm" VERSION=$(BANANAFRONT_VERSION)
endef

define BANANAFRONT_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/build/target/bananafront $(TARGET_DIR)/usr/bin/bananafront
endef

$(eval $(generic-package))

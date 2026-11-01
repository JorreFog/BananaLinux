################################################################################
#
# bananafront
#
################################################################################

# (local until it has a home on GitHub: the final step of the 2026-11-01 run points this at the pushed SHA)
# the BananaFront commit 0.5.1 was tested with: 47c7b85 (F opens the menu only as a tap; on top of e6cfc20, the GPU clock floor while a shader runs, KAN-144). Until
# its repository is pushed the site is local: build from a `git archive 47c7b85` of it (BANANAFRONT_SITE=<that folder>)
BANANAFRONT_VERSION = local
BANANAFRONT_SITE = $(HOME)/banana-emu/BananaFront
BANANAFRONT_SITE_METHOD = local
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

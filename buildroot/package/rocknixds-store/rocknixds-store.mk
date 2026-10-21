################################################################################
#
# rocknixds-store
#
################################################################################

# From ROCKNIXDS (store/ in JorreFog/ROCKNIXDS), the commit dodakvarter pins: the Store's app is built on Döda
# Kvarter's platform code (dodakvarter/src), and both stay in step. BananaOS's changes are a patch
# (buildroot/patches/rocknixds-store): RNDS_STORE_PLATFORM=banana in the package manager, -DBANANA in the app.
ROCKNIXDS_STORE_VERSION = ed9e3b543e6891ec4d75ceb490f73d37ebe84002
ROCKNIXDS_STORE_SITE = $(call github,JorreFog,ROCKNIXDS,$(ROCKNIXDS_STORE_VERSION))
ROCKNIXDS_STORE_LICENSE = MIT
ROCKNIXDS_STORE_LICENSE_FILES = LICENSE
ROCKNIXDS_STORE_DEPENDENCIES = libdrm sdl2

ROCKNIXDS_STORE_DK_SRC = plat.c plat_kms.c plat_sdl.c plat_headless.c input_evdev.c audio_alsa.c gfx.c font.c png.c

# as its build.sh, with Buildroot's GCC; SDL2's headers only (KMS runs it, SDL is loaded at run time elsewhere)
define ROCKNIXDS_STORE_BUILD_CMDS
	cd $(@D)/store && $(TARGET_CC) $(TARGET_CFLAGS) -O2 -w -DBANANA \
		-DSTORE_VERSION="\"$$(cat VERSION)-banana\"" -I../dodakvarter/src \
		-I$(STAGING_DIR)/usr/include/libdrm -I$(STAGING_DIR)/usr/include/SDL2 \
		-o store src/main.c src/job.c src/catalog.c $(addprefix ../dodakvarter/src/,$(ROCKNIXDS_STORE_DK_SRC)) \
		-ldrm -lm -lpthread -ldl
endef

# the app and its package manager; BananaOS's catalog beside it (store/ in BananaLinux: the copy used until the one on
# GitHub can be had), and the tile's one entry with its picture
define ROCKNIXDS_STORE_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/store/store $(TARGET_DIR)/usr/bin/rocknixds-store-ui
	$(INSTALL) -D -m 0755 $(@D)/store/device/rocknixds-store $(TARGET_DIR)/usr/share/banana/apps/store/rocknixds-store
	$(INSTALL) -m 0644 $(@D)/store/VERSION $(TARGET_DIR)/usr/share/banana/apps/store/VERSION
	$(INSTALL) -m 0644 $(BR2_EXTERNAL_BANANA_PATH)/../store/catalog.json $(TARGET_DIR)/usr/share/banana/apps/store/catalog.json
	$(INSTALL) -d $(TARGET_DIR)/usr/share/banana/apps/store/media
	for d in $(BR2_EXTERNAL_BANANA_PATH)/../store/apps/*/; do \
		$(INSTALL) -d $(TARGET_DIR)/usr/share/banana/apps/store/apps/$$(basename $$d) && \
		$(INSTALL) -m 0644 $$d*.png $(TARGET_DIR)/usr/share/banana/apps/store/apps/$$(basename $$d)/ || exit 1; \
	done
	$(INSTALL) -m 0644 $(ROCKNIXDS_STORE_PKGDIR)/store-image.png $(TARGET_DIR)/usr/share/banana/apps/store/media/store-image.png
	$(INSTALL) -m 0644 $(ROCKNIXDS_STORE_PKGDIR)/gamelist.xml $(TARGET_DIR)/usr/share/banana/apps/store/gamelist.xml
	touch "$(TARGET_DIR)/usr/share/banana/apps/store/Store.app"
endef

$(eval $(generic-package))

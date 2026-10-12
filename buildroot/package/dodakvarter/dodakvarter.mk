################################################################################
#
# dodakvarter
#
################################################################################

# From ROCKNIXDS (dodakvarter/ in JorreFog/ROCKNIXDS): 0.3.0, as in the RG DS Plus test build 7 (refs/testers/plus-test-7)
DODAKVARTER_VERSION = ed9e3b543e6891ec4d75ceb490f73d37ebe84002
DODAKVARTER_SITE = $(call github,JorreFog,ROCKNIXDS,$(DODAKVARTER_VERSION))
DODAKVARTER_LICENSE = MIT
DODAKVARTER_LICENSE_FILES = LICENSE
DODAKVARTER_DEPENDENCIES = libdrm sdl2

DODAKVARTER_SRC = main.c game.c mapgen.c world.c props.c render.c hud.c menu.c update.c radio.c music.c weapons.c \
	zombies.c loot.c inter.c audio.c save.c lang.c data.c bot.c art.c art_data.c gfx.c font.c png.c plat.c \
	plat_headless.c plat_kms.c plat_sdl.c selftest.c input_evdev.c audio_alsa.c boss.c splash.c

# as its build.sh, with Buildroot's GCC (-ffp-contract=off: the same floating point results as the tests); SDL2's
# headers only (the KMS backend runs it; SDL is loaded at run time for a window elsewhere)
define DODAKVARTER_BUILD_CMDS
	cd $(@D)/dodakvarter && $(TARGET_CC) $(TARGET_CFLAGS) -O2 -ffp-contract=off -w \
		-DDK_VERSION="\"$$(cat VERSION)\"" -I$(STAGING_DIR)/usr/include/libdrm -I$(STAGING_DIR)/usr/include/SDL2 \
		-o dodakvarter $(addprefix src/,$(DODAKVARTER_SRC)) -ldrm -lm -lpthread -ldl
endef

# the program, and its tile's one entry with the pictures the menu shows for it
define DODAKVARTER_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/dodakvarter/dodakvarter $(TARGET_DIR)/usr/bin/dodakvarter
	$(INSTALL) -d $(TARGET_DIR)/usr/share/banana/apps/dodakvarter/media
	$(INSTALL) -m 0644 $(@D)/dodakvarter/device/media/*.png $(TARGET_DIR)/usr/share/banana/apps/dodakvarter/media/
	$(INSTALL) -m 0644 $(DODAKVARTER_PKGDIR)/gamelist.xml $(TARGET_DIR)/usr/share/banana/apps/dodakvarter/gamelist.xml
	touch "$(TARGET_DIR)/usr/share/banana/apps/dodakvarter/Döda Kvarter.app"
endef

$(eval $(generic-package))

################################################################################
#
# retroarch
#
################################################################################

RETROARCH_VERSION = v1.22.2
RETROARCH_SITE = $(call github,libretro,RetroArch,$(RETROARCH_VERSION))
RETROARCH_LICENSE = GPL-3.0+
RETROARCH_LICENSE_FILES = COPYING
RETROARCH_DEPENDENCIES = host-pkgconf alsa-lib libdrm libegl libgles libgbm freetype zlib xz libudev-zero

# The joypad autoconfigs live in libretro's own repository, not in RetroArch's tree; its udev driver reads them from
# joypad_autoconfig_dir. The udev/ set is fetched here and installed below (8BitDo, Xbox and PlayStation pads); the
# RG DS's own pad is Banana's file, which wins. Pinned like the other sources.
RETROARCH_JOYPADS_VERSION = f3b62b847ed21939b668b897b18bbc1defbaae05
RETROARCH_EXTRA_DOWNLOADS = \
	https://github.com/libretro/retroarch-joypad-autoconfig/archive/$(RETROARCH_JOYPADS_VERSION).tar.gz

# RGUI's bitmap fonts (libretro/retroarch-assets rgui/font, ~250 KB): without them RetroArch's menu can't be switched to
# Russian, Belarusian, Ukrainian (patch 0005), Chinese, Japanese, Korean, Polish, Czech, Slovak, Turkish, Hungarian or
# Esperanto (it says "missing fonts" and stays in English). Installed in /usr/share/retroarch/assets, the assets folder
# the shipped retroarch.cfg names. Pinned like the other sources.
RETROARCH_ASSETS_VERSION = d9f969054dc7fbb6fa89519036d2b971e0855b51
RETROARCH_RGUI_FONTS = bitmap10x10_eng.bin bitmap10x10_rus.bin bitmap10x10_chn.bin bitmap10x10_jpn.bin \
	bitmap10x10_kor.bin bitmap6x10_eng.bin bitmap6x10_lse.bin
RETROARCH_EXTRA_DOWNLOADS += $(foreach f,$(RETROARCH_RGUI_FONTS),\
	https://raw.githubusercontent.com/libretro/retroarch-assets/$(RETROARCH_ASSETS_VERSION)/rgui/font/$(f))

# what a handheld with two KMS panels and no display server needs (built and linked for aarch64 with these options)
RETROARCH_CONF_OPTS = --prefix=/usr \
	--disable-x11 --disable-wayland --disable-pulse --disable-oss --disable-jack --disable-qt --disable-vulkan \
	--disable-ffmpeg --disable-sdl --disable-sdl2 --disable-systemd --disable-discord --disable-cdrom \
	--disable-materialui --disable-xmb --enable-ozone --enable-rgui \
	--enable-kms --enable-egl --enable-opengles --enable-opengles3 --enable-alsa --enable-udev --enable-freetype \
	--enable-networking --enable-threads --enable-zlib

define RETROARCH_CONFIGURE_CMDS
	cd $(@D) && $(TARGET_CONFIGURE_OPTS) CROSS_COMPILE="$(TARGET_CROSS)" PKG_CONF_PATH="$(PKG_CONFIG_HOST_BINARY)" \
		CFLAGS="$(TARGET_CFLAGS)" CXXFLAGS="$(TARGET_CXXFLAGS)" LDFLAGS="$(TARGET_LDFLAGS)" \
		./configure $(RETROARCH_CONF_OPTS)
endef

define RETROARCH_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D)
endef

# the autoconfig tarball beside RetroArch's sources (Buildroot only downloads it; it is not extracted by itself)
define RETROARCH_JOYPADS_EXTRACT
	mkdir -p $(@D)/joypads
	$(call suitable-extractor,$(RETROARCH_JOYPADS_VERSION).tar.gz) \
		$(RETROARCH_DL_DIR)/$(RETROARCH_JOYPADS_VERSION).tar.gz | \
		$(TAR) --strip-components=1 -C $(@D)/joypads $(TAR_OPTIONS) -
endef
RETROARCH_POST_EXTRACT_HOOKS += RETROARCH_JOYPADS_EXTRACT

# the binary, the RG DS's joypad as ROCKNIX maps it (F is the hotkey), and the udev autoconfigs for the pads people
# plug in: 8BitDo, Xbox and PlayStation (libretro/retroarch-joypad-autoconfig's udev/ set). The RG DS's own pad is
# Banana's file above; the udev driver picks the file matching the device, so a pad Banana doesn't name still works.
define RETROARCH_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/retroarch $(TARGET_DIR)/usr/bin/retroarch
	mkdir -p $(TARGET_DIR)/usr/share/libretro/autoconfig/udev
	$(INSTALL) -D -m 0644 $(RETROARCH_PKGDIR)/retrogame_joypad.cfg \
		$(TARGET_DIR)/usr/share/libretro/autoconfig/udev/retrogame_joypad.cfg
	if [ -d $(@D)/joypads/udev ]; then \
		find $(@D)/joypads/udev -maxdepth 1 -type f \
			\( -name '8BitDo*' -o -name '*X-Box*' -o -name '*Xbox*' -o -name '*XBOX*' \
			-o -name '*PlayStation*' -o -name '*DualShock*' -o -name '*DualSense*' \
			-o -name '*PS3*' -o -name '*PS4*' -o -name '*PS5*' -o -name '*Sony*' \) \
			-exec $(INSTALL) -m 0644 -t $(TARGET_DIR)/usr/share/libretro/autoconfig/udev {} + ; \
	fi
	mkdir -p $(TARGET_DIR)/usr/lib/libretro
	$(foreach f,$(RETROARCH_RGUI_FONTS),\
		$(INSTALL) -D -m 0644 $(RETROARCH_DL_DIR)/$(f) $(TARGET_DIR)/usr/share/retroarch/assets/rgui/font/$(f)$(sep))
	# the version, for Settings > System > About (RetroArch comes with BananaOS's updates; it has no updater of its own on Linux)
	printf '%s\n' '$(patsubst v%,%,$(RETROARCH_VERSION))' > $(TARGET_DIR)/usr/share/retroarch/version
endef

$(eval $(generic-package))

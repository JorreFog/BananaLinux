################################################################################
#
# retroarch
#
################################################################################

RETROARCH_VERSION = v1.22.2
RETROARCH_SITE = $(call github,libretro,RetroArch,$(RETROARCH_VERSION))
RETROARCH_LICENSE = GPL-3.0+
RETROARCH_LICENSE_FILES = COPYING
RETROARCH_DEPENDENCIES = host-pkgconf host-wayland alsa-lib libdrm libegl libgles libgbm freetype zlib xz libudev-zero vulkan-loader wayland libxkbcommon

# The joypad autoconfigs live in libretro's own repository, not in RetroArch's tree; its udev driver reads them from
# joypad_autoconfig_dir. The udev/ set is fetched here and installed below (8BitDo, Xbox and PlayStation pads); the
# handheld's own pad is Banana's file, which wins. Pinned like the other sources.
RETROARCH_JOYPADS_VERSION = f3b62b847ed21939b668b897b18bbc1defbaae05
RETROARCH_EXTRA_DOWNLOADS = \
	https://github.com/libretro/retroarch-joypad-autoconfig/archive/$(RETROARCH_JOYPADS_VERSION).tar.gz

# what a handheld with a KMS panel and no display server needs (built and linked for aarch64 with these options)
RETROARCH_CONF_OPTS = --prefix=/usr \
	--disable-x11 --enable-wayland --disable-pulse --disable-oss --disable-jack --disable-qt \
	--disable-ffmpeg --disable-sdl --disable-sdl2 --disable-systemd --disable-discord --disable-cdrom \
	--disable-materialui --disable-xmb --enable-ozone --enable-rgui \
	--enable-kms --enable-egl --enable-opengles --enable-opengles3 --enable-vulkan --enable-vulkan_display --enable-alsa --enable-udev --enable-freetype \
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

# the binary, and the udev autoconfigs for the pads people plug in: 8BitDo, Xbox and PlayStation
# (libretro/retroarch-joypad-autoconfig's udev/ set). The handheld's own pad is Banana's
# (system/rootfs/usr/share/libretro/autoconfig/udev/Retroid Pocket Classic Gamepad.cfg); the udev driver picks the
# file matching the device, so a pad Banana doesn't name still works.
define RETROARCH_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/retroarch $(TARGET_DIR)/usr/bin/retroarch
	mkdir -p $(TARGET_DIR)/usr/share/libretro/autoconfig/udev
	if [ -d $(@D)/joypads/udev ]; then \
		find $(@D)/joypads/udev -maxdepth 1 -type f \
			\( -name '8BitDo*' -o -name '*X-Box*' -o -name '*Xbox*' -o -name '*XBOX*' \
			-o -name '*PlayStation*' -o -name '*DualShock*' -o -name '*DualSense*' \
			-o -name '*PS3*' -o -name '*PS4*' -o -name '*PS5*' -o -name '*Sony*' \) \
			-exec $(INSTALL) -m 0644 -t $(TARGET_DIR)/usr/share/libretro/autoconfig/udev {} + ; \
	fi
	mkdir -p $(TARGET_DIR)/usr/lib/libretro
endef

$(eval $(generic-package))

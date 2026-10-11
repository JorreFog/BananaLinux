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

# the binary, and the RG DS's joypad as ROCKNIX maps it (F is the hotkey)
define RETROARCH_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/retroarch $(TARGET_DIR)/usr/bin/retroarch
	$(INSTALL) -D -m 0644 $(RETROARCH_PKGDIR)/retrogame_joypad.cfg \
		$(TARGET_DIR)/usr/share/libretro/autoconfig/udev/retrogame_joypad.cfg
	mkdir -p $(TARGET_DIR)/usr/lib/libretro
endef

$(eval $(generic-package))

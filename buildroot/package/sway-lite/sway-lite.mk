################################################################################
#
# sway-lite: sway for one program at a time (usr/libexec/banana/run-wayland)
#
################################################################################

# Buildroot's sway at the same version, without what a handheld's one-program session has no use for: swaybar and its
# tray (the one thing that needs sd-bus, and so systemd, in Buildroot's package), swaynag, the default wallpaper
# (swaybg), completions and man pages. The compositor and swaymsg stay (run-wayland ends the session with swaymsg).
SWAY_LITE_VERSION = 1.11
SWAY_LITE_SOURCE = sway-$(SWAY_LITE_VERSION).tar.gz
SWAY_LITE_SITE = https://github.com/swaywm/sway/releases/download/$(SWAY_LITE_VERSION)
SWAY_LITE_LICENSE = MIT
SWAY_LITE_LICENSE_FILES = LICENSE
SWAY_LITE_DEPENDENCIES = host-pkgconf wlroots json-c pcre2 cairo pango

SWAY_LITE_CONF_OPTS = \
	-Dwerror=false \
	-Dzsh-completions=false \
	-Dfish-completions=false \
	-Dbash-completions=false \
	-Dman-pages=disabled \
	-Dgdk-pixbuf=disabled \
	-Ddefault-wallpaper=false \
	-Dswaybar=false \
	-Dswaynag=false \
	-Dtray=disabled

# its own default configuration (a desktop's keys, a bar, a terminal) isn't used: run-wayland writes one per program
define SWAY_LITE_NO_DEFAULT_CONFIG
	rm -rf $(TARGET_DIR)/etc/sway $(TARGET_DIR)/usr/share/backgrounds/sway \
		$(TARGET_DIR)/usr/share/wayland-sessions/sway.desktop
endef
SWAY_LITE_POST_INSTALL_TARGET_HOOKS += SWAY_LITE_NO_DEFAULT_CONFIG

$(eval $(meson-package))

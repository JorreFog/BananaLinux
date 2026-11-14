################################################################################
#
# sdl3
#
################################################################################

# SDL 3, which Buildroot 2026.02 doesn't have: ARMSX2's frontend needs it (3.2.6 or newer), and Dolphin uses it when it
# is there. Built for the Thor as the emulators use it: KMS/DRM, Wayland (the compositor run-wayland starts for an
# emulator), GLES and Vulkan, ALSA (the image has no sound server for games), evdev pads through udev; no X11.
SDL3_VERSION = 3.2.30
SDL3_SITE = $(call github,libsdl-org,SDL,release-$(SDL3_VERSION))
SDL3_LICENSE = Zlib
SDL3_LICENSE_FILES = LICENSE.txt
SDL3_INSTALL_STAGING = YES
SDL3_DEPENDENCIES = host-pkgconf alsa-lib libdrm libgbm libegl libgles wayland wayland-protocols libxkbcommon \
	vulkan-headers $(if $(BR2_PACKAGE_HAS_UDEV),udev)
SDL3_CONF_OPTS = \
	-DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF -DSDL_INSTALL_TESTS=OFF \
	-DSDL_X11=OFF -DSDL_WAYLAND=ON -DSDL_WAYLAND_LIBDECOR=OFF -DSDL_KMSDRM=ON \
	-DSDL_OPENGL=OFF -DSDL_OPENGLES=ON -DSDL_VULKAN=ON \
	-DSDL_ALSA=ON -DSDL_PULSEAUDIO=OFF -DSDL_PIPEWIRE=OFF -DSDL_JACK=OFF -DSDL_SNDIO=OFF \
	-DSDL_DBUS=OFF -DSDL_IBUS=OFF -DSDL_RPATH=OFF

$(eval $(cmake-package))

################################################################################
#
# libretro-ppsspp
#
################################################################################

# PPSSPP's libretro core at v1.20.4 (ROCKNIX's pin). The PSP shelf uses the standalone PPSSPP (Vulkan) and Settings
# has no choice for it: this one is reached by a "run-retroarch ppsspp" line in the card's systems.conf.
# Only the core is installed, as in ROCKNIX: it looks for PPSSPP's assets (compat.ini, the flash0 fonts) in
# <system_directory>/PPSSPP, /data/bios/PPSSPP here, and says "Core system files missing" without them: run-retroarch
# shows the standalone's there (/usr/share/ppsspp/assets) while a game runs. It keeps its saves in RetroArch's save
# folder (<saves>/PSP), not the standalone's.
# FFmpeg: PPSSPP's own prebuilt one (its ffmpeg submodule), not the system's, as the standalone.
# Ported from ROCKNIX's ppsspp-lr (packages/emulators/libretro/ppsspp-lr at 6cf94dba, GPL-2.0):
# the same commit, build options and patches.
LIBRETRO_PPSSPP_VERSION = fa50bb1976065c4f8b1b47af227d367fe9771555
LIBRETRO_PPSSPP_SITE = https://github.com/hrydgard/ppsspp.git
LIBRETRO_PPSSPP_SITE_METHOD = git
LIBRETRO_PPSSPP_GIT_SUBMODULES = YES
LIBRETRO_PPSSPP_LICENSE = GPL-2.0+
LIBRETRO_PPSSPP_LICENSE_FILES = LICENSE.TXT
LIBRETRO_PPSSPP_DEPENDENCIES = libgles libegl
# Patches (beside this file, applied by Buildroot in order; each says where it comes from):
# - 0001-psp-path.patch (ROCKNIX's 002-psp-path.patch): the memory stick is always <dir>/PSP
# - 0002-build-flags.patch (ROCKNIX's 004-build-flags.patch): -Ofast without SLP vectorisation
# - 0003-fix-atomics-arm64.patch (ROCKNIX's 005-fix-atomics-arm64.patch): no outline atomics

LIBRETRO_PPSSPP_CONF_OPTS = -DLIBRETRO=ON -DUSING_GLES2=ON -DUSING_EGL=OFF -DUSING_FBDEV=ON -DVULKAN=OFF -DUSE_VULKAN_DISPLAY_KHR=OFF -DUSING_X11_VULKAN=OFF -DUSE_WAYLAND_WSI=OFF -DUSE_SYSTEM_FFMPEG=OFF -DUSE_SYSTEM_LIBZIP=OFF -DFORCED_CPU=aarch64 -DBUILD_SHARED_LIBS=OFF -DUSING_QT_UI=OFF -DUNITTEST=OFF -DSIMULATOR=OFF -DHEADLESS=OFF -DUSE_DISCORD=OFF -DCMAKE_BUILD_TYPE=Release

define LIBRETRO_PPSSPP_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $$(find $(@D) -name ppsspp_libretro.so | head -1) $(TARGET_DIR)/usr/lib/libretro/ppsspp_libretro.so
endef

$(eval $(cmake-package))

################################################################################
#
# ppsspp: the PSP emulator, standalone (SDL2, KMSDRM, GLES), as ROCKNIX runs it. The release's source tarball has
# the submodules in it (a git download of them would be gigabytes: the FFmpeg one keeps every platform's prebuilt
# libraries), all but those prebuilt FFmpeg libraries: its FFmpeg is built here first, from the tarball's own source
# with PPSSPP's own options (ffmpeg/linux_arm64.sh: only the PSP's codecs, static), where its CMake looks for it.
# The patches are ROCKNIX's (packages/emulators/standalone/ppsspp-sa): a real fullscreen on KMSDRM, no mouse
# cursor, no Gold or web links, and the pad's guide button (F) a key of its own for the hotkeys, not the pause menu.
#
################################################################################

PPSSPP_VERSION = 1.20.4
PPSSPP_SOURCE = ppsspp-$(PPSSPP_VERSION).tar.xz
PPSSPP_SITE = https://github.com/hrydgard/ppsspp/releases/download/v$(PPSSPP_VERSION)
PPSSPP_LICENSE = GPL-2.0+
PPSSPP_LICENSE_FILES = LICENSE.TXT
PPSSPP_DEPENDENCIES = sdl2 sdl2_ttf libgles libegl zlib host-pkgconf
PPSSPP_SUPPORTS_IN_SOURCE_BUILD = NO

define PPSSPP_BUILD_FFMPEG
	cd $(@D)/ffmpeg && $(SED) 's|--cc=aarch64-linux-gnu-gcc|--cc=$(TARGET_CC)|; s|--cross-prefix=aarch64-linux-gnu-|--cross-prefix=$(TARGET_CROSS)|; s|--nm=aarch64-linux-gnu-gcc-nm|--nm=$(TARGET_NM)|; s|^make install|$(MAKE) install|' linux_arm64.sh
	cd $(@D)/ffmpeg && $(TARGET_MAKE_ENV) bash ./linux_arm64.sh
endef
PPSSPP_PRE_CONFIGURE_HOOKS += PPSSPP_BUILD_FFMPEG

# ROCKNIX's GLES build (no Vulkan: Banana's Mesa is Panfrost's GL), its bundled FFmpeg and libraries
# SDL2_ttf by pkg-config (PPSSPP's fallback): the cmake file SDL2_ttf's autotools build installs has no SDL2_ttf::SDL2_ttf target
PPSSPP_CONF_OPTS = \
	-DCMAKE_BUILD_TYPE=Release \
	-DUSING_FBDEV=ON \
	-DUSING_EGL=OFF \
	-DUSING_GLES2=ON \
	-DVULKAN=OFF \
	-DUSE_VULKAN_DISPLAY_KHR=OFF \
	-DUSE_WAYLAND_WSI=OFF \
	-DUSING_X11_VULKAN=OFF \
	-DUSE_SYSTEM_FFMPEG=OFF \
	-DUSE_SYSTEM_LIBPNG=OFF \
	-DUSE_DISCORD=OFF \
	-DUSING_QT_UI=OFF \
	-DHEADLESS=OFF \
	-DUNITTEST=OFF \
	-DSIMULATOR=OFF \
	-DBUILD_SHARED_LIBS=OFF \
	-DCMAKE_DISABLE_FIND_PACKAGE_SDL2_ttf=ON

PPSSPP_MAKE_OPTS = PPSSPPSDL

# the program beside its assets: it looks for the PSP's flash0 (its system fonts) in assets/ next to itself
define PPSSPP_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(PPSSPP_BUILDDIR)/PPSSPPSDL $(TARGET_DIR)/usr/share/ppsspp/PPSSPPSDL
	rm -rf $(TARGET_DIR)/usr/share/ppsspp/assets
	cp -a $(PPSSPP_BUILDDIR)/assets $(TARGET_DIR)/usr/share/ppsspp/assets
endef

$(eval $(cmake-package))

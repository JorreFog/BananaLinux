################################################################################
#
# ppsspp
#
################################################################################

# PPSSPP, the PSP emulator, its SDL 2 front end (PPSSPPSDL) on Wayland with Vulkan, as ROCKNIX runs it on the SM8550.
# Ported from ROCKNIX's ppsspp-sa (packages/emulators/standalone/ppsspp-sa at 6cf94dba, GPL-2.0): the same commit
# (v1.20.2), its build options for a Wayland, GLES and Vulkan device, its patches (0001 sound not muted when the window
# isn't focused, 0002 full screen on DRM, 0003 no mouse cursor, 0004 fixed folders, written as BananaOS's: settings in
# /data/ppsspp, games and the memory stick in /data/roms/psp, states in /data/states/psp; 0005 menu items the handheld
# can't use hidden, 0006 the Home button doesn't pause) and its SM8550 settings (ppsspp.ini: Vulkan). FFmpeg is
# PPSSPP's own (its ffmpeg submodule's prebuilt libraries), as ROCKNIX builds it. ROCKNIX's cheat database (a download
# at its install step) and its Japanese font aren't included.
PPSSPP_VERSION = afbc66a318b86432642b532c575241f3716642ef
PPSSPP_SITE = https://github.com/hrydgard/ppsspp.git
PPSSPP_SITE_METHOD = git
PPSSPP_GIT_SUBMODULES = YES
PPSSPP_LICENSE = GPL-2.0+
PPSSPP_LICENSE_FILES = LICENSE.TXT
PPSSPP_SUPPORTS_IN_SOURCE_BUILD = NO
PPSSPP_DEPENDENCIES = host-pkgconf sdl2 libzip zlib libegl libgles vulkan-headers wayland
PPSSPP_CONF_OPTS = \
	-DCMAKE_BUILD_TYPE=Release -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_CROSSCOMPILING=ON \
	-DUSE_SYSTEM_FFMPEG=OFF -DUSE_SYSTEM_LIBPNG=OFF -DBUILD_SHARED_LIBS=OFF \
	-DANDROID=OFF -DWIN32=OFF -DAPPLE=OFF -DUSING_QT_UI=OFF -DUNITTEST=OFF -DSIMULATOR=OFF -DHEADLESS=OFF \
	-DUSE_DISCORD=OFF -DLIBRETRO=OFF \
	-DUSING_FBDEV=ON -DUSING_EGL=OFF -DUSING_GLES2=ON \
	-DVULKAN=ON -DUSE_VULKAN_DISPLAY_KHR=ON -DUSING_X11_VULKAN=OFF -DUSE_WAYLAND_WSI=ON \
	-DCMAKE_C_FLAGS="$(TARGET_CFLAGS) -Wno-error -DEGL_NO_X11=1 -DMESA_EGL_NO_X11_HEADERS=1" \
	-DCMAKE_CXX_FLAGS="$(TARGET_CXXFLAGS) -Wno-error -DEGL_NO_X11=1 -DMESA_EGL_NO_X11_HEADERS=1"

# ROCKNIX's pre-configure edits: no -O2/-O3 of its own over Buildroot's flags, the DRM headers from the sysroot
define PPSSPP_ROCKNIX_FIXUPS
	$(SED) 's/\-O[23]//g' $(@D)/CMakeLists.txt
	$(SED) 's|include_directories(/usr/include/drm)|include_directories($(STAGING_DIR)/usr/include/drm)|' $(@D)/CMakeLists.txt
endef
PPSSPP_POST_PATCH_HOOKS += PPSSPP_ROCKNIX_FIXUPS

# the program, PPSSPP's assets (fonts, shaders, the game controller database it ships, translations) beside the
# templates in /usr/share/ppsspp (patch 0004 reads them from /data/ppsspp/assets, which run-ppsspp links there)
define PPSSPP_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/buildroot-build/PPSSPPSDL $(TARGET_DIR)/usr/bin/ppsspp
	$(call THOR_EMULATORS_INSTALL_TEMPLATES,ppsspp)
	cp -a $(@D)/buildroot-build/assets $(TARGET_DIR)/usr/share/ppsspp/assets
endef

$(eval $(cmake-package))

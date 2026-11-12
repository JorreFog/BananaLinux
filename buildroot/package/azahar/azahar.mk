################################################################################
#
# azahar
#
################################################################################

# Azahar, the Nintendo 3DS emulator (Citra's successor), its Qt 6 front end on Wayland: the two 3DS screens in two
# windows ("Separate windows", layout_option 4), the second one titled "... Secondary ...", which run-wayland
# --two-screens moves to the Thor's bottom panel. Ported from ROCKNIX's azahar-sa (packages/emulators/standalone/
# azahar-sa at 6cf94dba, GPL-2.0): the same commit (tag 2126.2), its build options and two of its patches (0001: the
# data folder named azahar, the log in BananaOS's log folder; 0002: the native surface released before Wayland
# destroys it). Its third, prefer_gles, isn't used: ROCKNIX applies it only on GLES-first devices; the Thor runs
# Vulkan. Added here: USE_SYSTEM_QT (Buildroot's Qt, not a Qt download at configure time). The submodules (dynarmic,
# the bundled SDL2, boost, cubeb...) come with the git download. -Wno-error=incompatible-pointer-types is ROCKNIX's.
AZAHAR_VERSION = 219d743a1e698101fe1ab8bef43e6563126c3c27
AZAHAR_SITE = https://github.com/azahar-emu/azahar.git
AZAHAR_SITE_METHOD = git
AZAHAR_GIT_SUBMODULES = YES
AZAHAR_LICENSE = GPL-2.0+
AZAHAR_LICENSE_FILES = license.txt
AZAHAR_SUPPORTS_IN_SOURCE_BUILD = NO
AZAHAR_DEPENDENCIES = host-pkgconf qt6base qt6multimedia qt6wayland libegl libgles vulkan-headers alsa-lib \
	libusb zstd ffmpeg
AZAHAR_CONF_OPTS = \
	-DCMAKE_BUILD_TYPE=Release \
	-DENABLE_QT=ON -DUSE_SYSTEM_QT=ON -DENABLE_QT_TRANSLATION=OFF -DENABLE_QT_UPDATE_CHECKER=OFF \
	-DENABLE_SDL2=ON -DUSE_SYSTEM_SDL2=OFF -DENABLE_OPENGL=ON -DENABLE_VULKAN=ON \
	-DENABLE_ROOM=OFF -DENABLE_ROOM_STANDALONE=OFF -DENABLE_TESTS=OFF -DENABLE_WEB_SERVICE=OFF \
	-DUSE_DISCORD_PRESENCE=OFF -DENABLE_DISCORD_RPC=OFF -DENABLE_LIBRETRO=OFF \
	-DCITRA_WARNINGS_AS_ERRORS=OFF -DCITRA_USE_PRECOMPILED_HEADERS=OFF \
	-DCMAKE_C_FLAGS="$(TARGET_CFLAGS) -Wno-error=incompatible-pointer-types"

# the emulator and ROCKNIX's settings (config/qt-config.ini). Its pad bindings aren't ROCKNIX's, which name the one
# pad InputPlumber makes there (a virtual DualSense, by its GUID and raw button numbers): they are SDL game-controller
# buttons and axes on every pad (api:controller, maptype:all), mapped by the list run-azahar hands SDL
define AZAHAR_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/buildroot-build/bin/Release/azahar $(TARGET_DIR)/usr/bin/azahar
	$(call THOR_EMULATORS_INSTALL_TEMPLATES,azahar)
endef

$(eval $(cmake-package))

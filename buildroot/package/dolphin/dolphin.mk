################################################################################
#
# dolphin
#
################################################################################

# Dolphin, the GameCube and Wii emulator: its "nogui" player only (no Qt), on Wayland (the compositor run-wayland starts
# for it) with Vulkan through Turnip, ALSA sound, evdev and SDL pads. Ported from ROCKNIX's dolphin-sa
# (packages/emulators/standalone/dolphin-sa at 6cf94dba, GPL-2.0): the same commit (release 2606), its patches in its
# order (0001-0017: hotkeys, pad order, save-state names, the BIOS folder, the Wayland platform for nogui and EGL/Vulkan
# on Wayland; the Qt ones are kept so the tree matches ROCKNIX's, though Qt isn't built) and its build options, except:
# no Qt front end (ROCKNIX builds it for its settings screen), no X11, no PulseAudio, no OpenAL.
DOLPHIN_VERSION = 6094cfcf7b8fba733b3116fdf3414d51c1c0e4a4
DOLPHIN_SITE = https://github.com/dolphin-emu/dolphin.git
DOLPHIN_SITE_METHOD = git
DOLPHIN_GIT_SUBMODULES = YES
DOLPHIN_LICENSE = GPL-2.0+
DOLPHIN_LICENSE_FILES = COPYING
DOLPHIN_SUPPORTS_IN_SOURCE_BUILD = NO
DOLPHIN_DEPENDENCIES = host-pkgconf libevdev libdrm zlib libpng lzo libusb zstd alsa-lib libcurl \
	libegl libgles wayland wayland-protocols libxkbcommon vulkan-headers vulkan-loader \
	$(if $(BR2_PACKAGE_HAS_UDEV),udev)

# what ROCKNIX's post_unpack does: the cross ar/ranlib for the LTO archives, two includes the bundled
# VulkanMemoryAllocator lacks with GCC 14, and the version its patch 0007 leaves as placeholders (2606.1)
define DOLPHIN_ROCKNIX_FIXUPS
	$(SED) 's|gcc-ar|$(TARGET_CROSS)gcc-ar|g; s|gcc-ranlib|$(TARGET_CROSS)gcc-ranlib|g' $(@D)/CMakeLists.txt
	$(SED) 's~#include <cstdlib>~#include <cstdlib>\n#include <cstdint>~' \
		$(@D)/Externals/VulkanMemoryAllocator/include/vk_mem_alloc.h
	$(SED) 's~#include <cstdint>~#include <cstdint>\n#include <string>~' \
		$(@D)/Externals/VulkanMemoryAllocator/include/vk_mem_alloc.h
	$(SED) 's/@PKG_DOLPHIN_VERSION_MAJOR@/2606/; s/@PKG_DOLPHIN_VERSION_MINOR@/1/' $(@D)/CMake/ScmRevGen.cmake
endef
DOLPHIN_POST_PATCH_HOOKS += DOLPHIN_ROCKNIX_FIXUPS

DOLPHIN_CONF_OPTS = \
	-DCMAKE_BUILD_TYPE=Release -DDISTRIBUTOR=BananaOS \
	-DENABLE_QT=OFF -DENABLE_NOGUI=ON -DENABLE_HEADLESS=OFF -DENABLE_CLI_TOOL=OFF \
	-DENABLE_X11=OFF -DENABLE_WAYLAND=ON -DENABLE_EGL=ON -DENABLE_VULKAN=ON \
	-DENABLE_EVDEV=ON -DENABLE_SDL=ON -DSDL_PIPEWIRE=OFF \
	-DENABLE_ALSA=ON -DENABLE_PULSEAUDIO=OFF -DENABLE_CUBEB=ON \
	-DUSE_RETRO_ACHIEVEMENTS=ON -DUSE_DISCORD_PRESENCE=OFF -DUSE_MGBA=OFF -DENCODE_FRAMEDUMPS=OFF \
	-DENABLE_LLVM=OFF -DENABLE_TESTS=OFF -DENABLE_ANALYTICS=OFF -DENABLE_AUTOUPDATE=OFF \
	-DENABLE_LTO=ON -DBUILD_SHARED_LIBS=OFF -DLINUX_LOCAL_DEV=OFF -DMBEDTLS_FATAL_WARNINGS=OFF \
	-DCMAKE_POLICY_VERSION_MINIMUM=3.5

# CMake's install: /usr/bin/dolphin-emu-nogui and Dolphin's own data (/usr/share/dolphin-emu/sys). Then ROCKNIX's
# SM8550 settings (Vulkan, 2x internal resolution, the pad and Wii Remote profiles) as /usr/share/dolphin, which
# run-dolphin copies into /data/dolphin-emu on the first run
define DOLPHIN_INSTALL_BANANA
	$(call THOR_EMULATORS_INSTALL_TEMPLATES,dolphin)
endef
DOLPHIN_POST_INSTALL_TARGET_HOOKS += DOLPHIN_INSTALL_BANANA

$(eval $(cmake-package))

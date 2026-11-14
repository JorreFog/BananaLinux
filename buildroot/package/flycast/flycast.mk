################################################################################
#
# flycast
#
################################################################################

# Flycast, the Dreamcast, Naomi and Atomiswave emulator, standalone (SDL 2, Vulkan and GLES), on Wayland: ROCKNIX's
# default for those three systems on the SM8550. Ported from ROCKNIX's flycast-sa (packages/emulators/standalone/
# flycast-sa at 6cf94dba, GPL-2.0): the same commit (v2.7), its build options for a GLES and Vulkan device and its
# patches (0001 fixed folders, written as BananaOS's: settings in /data/flycast, the BIOS files in /data/bios/dc;
# 0002 a GCC 15 fix in the bundled glslang; 0003 SDL game controllers), and its SM8550 settings (emu.cfg: Vulkan),
# with ALSA instead of PulseAudio. Its submodules (glslang, libchdr, breakpad...) come with the git download.
FLYCAST_VERSION = 5aa091fde632fb332c8d8c34e280d62dc951954c
FLYCAST_SITE = https://github.com/flyinghead/flycast.git
FLYCAST_SITE_METHOD = git
FLYCAST_GIT_SUBMODULES = YES
FLYCAST_LICENSE = GPL-2.0+
FLYCAST_LICENSE_FILES = LICENSE
FLYCAST_SUPPORTS_IN_SOURCE_BUILD = NO
FLYCAST_DEPENDENCIES = host-pkgconf sdl2 libzip libcurl libminiupnpc lua alsa-lib libegl libgles vulkan-headers
# BUILD_SHARED_LIBS off (Flycast's own default; Buildroot passes ON): its bundled libraries (nowide, xxHash...) would
# otherwise be built as shared objects that stay in the build folder, and flycast wouldn't start without them
FLYCAST_CONF_OPTS = -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DUSE_GLES=ON -DUSE_OPENGL=ON -DUSE_VULKAN=ON \
	-DUSE_OPENMP=ON -DUSE_HOST_SDL=ON -DUSE_HOST_LIBZIP=ON -DUSE_ALSA=ON -DUSE_PULSEAUDIO=OFF -DUSE_LIBAO=OFF -DUSE_LUA=ON \
	-DUSE_BREAKPAD=OFF -DUSE_DISCORD=OFF -DLIBRETRO=OFF \
	-DCMAKE_CXX_FLAGS="$(TARGET_CXXFLAGS) -Wno-error=array-bounds"

# ROCKNIX's post_unpack: -Ofast where the project asks for -O2/-O3
define FLYCAST_ROCKNIX_FIXUPS
	$(SED) 's/\-O[23]/-Ofast/' $(@D)/CMakeLists.txt
endef
FLYCAST_POST_PATCH_HOOKS += FLYCAST_ROCKNIX_FIXUPS

define FLYCAST_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/buildroot-build/flycast $(TARGET_DIR)/usr/bin/flycast
	$(call THOR_EMULATORS_INSTALL_TEMPLATES,flycast)
endef

$(eval $(cmake-package))

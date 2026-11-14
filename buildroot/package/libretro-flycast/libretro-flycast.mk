################################################################################
#
# libretro-flycast
#
################################################################################

# Flycast's libretro core. The Dreamcast shelf uses the standalone Flycast, as ROCKNIX does on the SM8550, and
# Settings has no choice for it: this one is for RetroArch's netplay and achievements, by a "run-retroarch flycast"
# line in the card's systems.conf. GLES on, as both USE_OPENGL and USE_GLES must be (ROCKNIX's note); no Vulkan
# (RetroArch here has none).
# Ported from ROCKNIX's flycast-lr (packages/emulators/libretro/flycast-lr at 6cf94dba, GPL-2.0):
# the same commit and build options.
LIBRETRO_FLYCAST_VERSION = 7adbf3caef251b153e348df3f469a0f35878339d
LIBRETRO_FLYCAST_SITE = https://github.com/flyinghead/flycast.git
LIBRETRO_FLYCAST_SITE_METHOD = git
LIBRETRO_FLYCAST_GIT_SUBMODULES = YES
LIBRETRO_FLYCAST_LICENSE = GPL-2.0+
LIBRETRO_FLYCAST_LICENSE_FILES = LICENSE
# libzip: the bundled one (the libretro build never looks for the system's).
LIBRETRO_FLYCAST_DEPENDENCIES = zlib libgles libegl

# BUILD_SHARED_LIBS=OFF: Buildroot's cmake default is ON, which makes the bundled nowide and libzip shared libraries
# (nowide.so.11.3.0, zip.so.5) left in the build folder, and the core then cannot be loaded.
LIBRETRO_FLYCAST_CONF_OPTS = -DLIBRETRO=ON -DBUILD_SHARED_LIBS=OFF -DUSE_OPENGL=ON -DUSE_GLES=ON -DUSE_VULKAN=OFF -DWITH_SYSTEM_ZLIB=ON -DUSE_OPENMP=ON -DCMAKE_BUILD_TYPE=Release

define LIBRETRO_FLYCAST_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $$(find $(@D) -name flycast_libretro.so | head -1) $(TARGET_DIR)/usr/lib/libretro/flycast_libretro.so
endef

$(eval $(cmake-package))

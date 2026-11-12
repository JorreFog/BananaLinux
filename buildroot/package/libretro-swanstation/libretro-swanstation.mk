################################################################################
#
# libretro-swanstation
#
################################################################################

# SwanStation: the libretro fork of DuckStation made while DuckStation was GPL-3.0. DuckStation itself is not
# packaged: its licence since 2024 (CC BY-NC-ND 4.0) forbids redistributing modified builds and packaging it in an image.
# RetroArch here draws with GLES (no Vulkan), so SwanStation's OpenGL ES renderer is the one used.
# Ported from ROCKNIX's swanstation-lr (packages/emulators/libretro/swanstation-lr at 6cf94dba, GPL-2.0):
# the same commit, build options and patches.
LIBRETRO_SWANSTATION_VERSION = 631f0fb31b809e0f889d7c551fe695788e39c1d3
LIBRETRO_SWANSTATION_SITE = $(call github,libretro,swanstation,$(LIBRETRO_SWANSTATION_VERSION))
LIBRETRO_SWANSTATION_LICENSE = GPL-3.0+
LIBRETRO_SWANSTATION_LICENSE_FILES = LICENSE
LIBRETRO_SWANSTATION_DEPENDENCIES = libgles libegl
# Patches (beside this file, applied by Buildroot in order; each says where it comes from):
# - 0001-vulkan-warm-only-active-filter.patch (ROCKNIX's 001-vulkan-warm-only-active-filter.patch): ROCKNIX's;
#   only touches the Vulkan renderer, which RetroArch here doesn't have (kept so the tree matches ROCKNIX's)

LIBRETRO_SWANSTATION_CONF_OPTS = -DBUILD_LIBRETRO_CORE=ON -DCMAKE_BUILD_TYPE=Release

define LIBRETRO_SWANSTATION_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $$(find $(@D) -name swanstation_libretro.so | head -1) $(TARGET_DIR)/usr/lib/libretro/swanstation_libretro.so
endef

$(eval $(cmake-package))

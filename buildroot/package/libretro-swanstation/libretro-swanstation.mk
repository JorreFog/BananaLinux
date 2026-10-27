################################################################################
#
# libretro-swanstation
#
################################################################################

# ROCKNIX's swanstation-lr package: upstream libretro/swanstation at ROCKNIX's
# pinned commit, built as a libretro core with CMake and linked against the
# panel's EGL/GLES (mesa3d-freedreno).
LIBRETRO_SWANSTATION_VERSION = 631f0fb31b809e0f889d7c551fe695788e39c1d3
LIBRETRO_SWANSTATION_SITE = https://github.com/libretro/swanstation
LIBRETRO_SWANSTATION_SITE_METHOD = git
LIBRETRO_SWANSTATION_LICENSE = GPL-3.0+
LIBRETRO_SWANSTATION_LICENSE_FILES = LICENSE
LIBRETRO_SWANSTATION_DEPENDENCIES = host-nasm

LIBRETRO_SWANSTATION_CONF_OPTS = \
	-DBUILD_LIBRETRO_CORE=ON \
	-DBUILD_SHARED_LIBS=OFF \
	-DCMAKE_POSITION_INDEPENDENT_CODE=ON

define LIBRETRO_SWANSTATION_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/swanstation_libretro.so \
		$(TARGET_DIR)/usr/lib/libretro/swanstation_libretro.so
endef

$(eval $(cmake-package))

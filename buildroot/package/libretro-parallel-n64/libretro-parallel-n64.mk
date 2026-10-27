################################################################################
#
# libretro-parallel-n64
#
################################################################################

# ROCKNIX's parallel-n64-lr: upstream libretro/parallel-n64 at ROCKNIX's pinned
# commit, built for the handheld's GLES with the AArch64 dynarec.
LIBRETRO_PARALLEL_N64_VERSION = f8605345e13c018a30c8f4ed03c05d8fc8f70be8
LIBRETRO_PARALLEL_N64_SITE = $(call github,libretro,parallel-n64,$(LIBRETRO_PARALLEL_N64_VERSION))
LIBRETRO_PARALLEL_N64_LICENSE = GPL-2.0+
LIBRETRO_PARALLEL_N64_LICENSE_FILES = COPYING

define LIBRETRO_PARALLEL_N64_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) \
		platform=banana ARCH=aarch64 GLES=1 GL_LIB="-lGLESv2" \
		CC="$(TARGET_CC)" CXX="$(TARGET_CXX)"
endef

define LIBRETRO_PARALLEL_N64_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/parallel_n64_libretro.so \
		$(TARGET_DIR)/usr/lib/libretro/parallel_n64_libretro.so
endef

$(eval $(generic-package))

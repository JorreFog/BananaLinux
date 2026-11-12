################################################################################
#
# libretro-parallel-n64
#
################################################################################

# ParaLLEl N64 with its GLES renderers (its Vulkan RDP needs RetroArch's Vulkan driver, which isn't built here).
# Ported from ROCKNIX's parallel-n64-lr (packages/emulators/libretro/parallel-n64-lr at 6cf94dba, GPL-2.0):
# the same commit, build options and patches.
LIBRETRO_PARALLEL_N64_VERSION = 804135cf2fa14e26e73b889c4362c6cda624c842
LIBRETRO_PARALLEL_N64_SITE = $(call github,libretro,parallel-n64,$(LIBRETRO_PARALLEL_N64_VERSION))
LIBRETRO_PARALLEL_N64_LICENSE = GPL-2.0+
LIBRETRO_PARALLEL_N64_LICENSE_FILES = README.md mupen64plus-core/LICENSES
# Patches (beside this file, applied by Buildroot in order; each says where it comes from):
# - 0001-platform.patch (ROCKNIX's 002-platform.patch): with its @DEVICE@ placeholder (the device it builds for)
#   written as "thor": the aarch64 dynarec and the ARM fixes for platform=thor

define LIBRETRO_PARALLEL_N64_BUILD_CMDS
	$(TARGET_MAKE_ENV) CFLAGS="$(TARGET_CFLAGS) -std=gnu17" $(MAKE) -C $(@D) platform=thor GLES=1 GL_LIB=-lGLESv2 CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" AR="$(TARGET_AR)"
endef

define LIBRETRO_PARALLEL_N64_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/parallel_n64_libretro.so $(TARGET_DIR)/usr/lib/libretro/parallel_n64_libretro.so
endef

$(eval $(generic-package))

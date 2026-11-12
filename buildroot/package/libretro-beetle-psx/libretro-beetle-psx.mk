################################################################################
#
# libretro-beetle-psx
#
################################################################################

# Beetle PSX, the software-rendered build ROCKNIX ships (mednafen_psx). The HW build's renderers want desktop OpenGL
# 3.3 or Vulkan, which RetroArch here (GLES on KMS) doesn't give a core.
# Ported from ROCKNIX's beetle-psx-lr (packages/emulators/libretro/beetle-psx-lr at 6cf94dba, GPL-2.0):
# the same commit and build options.
# Its Makefile asks the build machine what it is (uname): a PC turns the x86 flags on and doesn't set the software
# GPU's NEON switch, whatever the target. Both are said here instead: not x86, NEON (every ARM64 core has it; the GPU
# code also looks for it by itself, so this only makes sure).
LIBRETRO_BEETLE_PSX_VERSION = fb39fbfd8eb8ec36cd040b7e3f6aae6add05a805
LIBRETRO_BEETLE_PSX_SITE = $(call github,libretro,beetle-psx-libretro,$(LIBRETRO_BEETLE_PSX_VERSION))
LIBRETRO_BEETLE_PSX_LICENSE = GPL-2.0+
LIBRETRO_BEETLE_PSX_LICENSE_FILES = COPYING

define LIBRETRO_BEETLE_PSX_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) platform=unix LINK_STATIC_LIBCPLUSPLUS=0 IS_X86=0 \
		CC="$(TARGET_CC) -DGPU_HAVE_NEON=1" CXX="$(TARGET_CXX) -DGPU_HAVE_NEON=1" AR="$(TARGET_AR)"
endef

define LIBRETRO_BEETLE_PSX_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/mednafen_psx_libretro.so $(TARGET_DIR)/usr/lib/libretro/mednafen_psx_libretro.so
endef

$(eval $(generic-package))

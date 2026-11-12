################################################################################
#
# libretro-same-cdi
#
################################################################################

# SAME_CDI: MAME's CD-i driver as a libretro core, built as ROCKNIX builds it (MAME's own makefile, genie with the
# host's compiler), its make options verbatim. Slow to compile (a MAME tree).
# Ported from ROCKNIX's same_cdi-lr (packages/emulators/libretro/same_cdi-lr at 6cf94dba, GPL-2.0):
# the same commit, build options and patches.
LIBRETRO_SAME_CDI_VERSION = ff9bb99a50d518aef6d2add1c49b7c44447e3e78
LIBRETRO_SAME_CDI_SITE = $(call github,libretro,same_cdi,$(LIBRETRO_SAME_CDI_VERSION))
LIBRETRO_SAME_CDI_LICENSE = GPL-2.0+
LIBRETRO_SAME_CDI_LICENSE_FILES = COPYING
LIBRETRO_SAME_CDI_DEPENDENCIES = host-python3 expat zlib flac libogg sqlite
# Patches (beside this file, applied by Buildroot in order; each says where it comes from):
# - 0001-gcc13-fix.patch (ROCKNIX's 001-gcc13-fix.patch): a missing <cstdint>

define LIBRETRO_SAME_CDI_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) -f Makefile.libretro REGENIE=1 VERBOSE=1 NOWERROR=1 OPENMP=1 CROSS_BUILD=1 TOOLS=0 RETRO=1 PTR64=0 NOASM=0 PYTHON_EXECUTABLE=$(HOST_DIR)/bin/python3 CONFIG=libretro LIBRETRO_OS=unix LIBRETRO_CPU= PLATFORM=arm64 ARCH= OSD=retro USE_SYSTEM_LIB_EXPAT=1 USE_SYSTEM_LIB_ZLIB=1 USE_SYSTEM_LIB_FLAC=FLAC,ogg USE_SYSTEM_LIB_SQLITE3=1 OVERRIDE_CC="$(TARGET_CC)" OVERRIDE_CXX="$(TARGET_CXX)" OVERRIDE_LD="$(TARGET_LD)" ARCHOPTS="-D__aarch64__ -DASMJIT_BUILD_X86"
endef

define LIBRETRO_SAME_CDI_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/same_cdi_libretro.so $(TARGET_DIR)/usr/lib/libretro/same_cdi_libretro.so
endef

$(eval $(generic-package))

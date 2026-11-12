################################################################################
#
# libretro-same-cdi
#
################################################################################

# SAME_CDI: MAME's CD-i driver as a libretro core. Slow to compile (a MAME tree). At this commit the tree no longer
# has MAME's genie build: Makefile.libretro is a plain makefile that reads CC, CXX and AR (ROCKNIX's genie options,
# OVERRIDE_CC and the rest, are ignored by it and would leave the build machine's cc). It compiles only sources in
# its own tree and links no library but libc's, so expat, zlib, flac and sqlite are not dependencies, nor is
# host-python3 (the generated files are committed). No LD: the makefile links with $(CXX).
# Ported from ROCKNIX's same_cdi-lr (packages/emulators/libretro/same_cdi-lr at 6cf94dba, GPL-2.0):
# the same commit and patches.
LIBRETRO_SAME_CDI_VERSION = ff9bb99a50d518aef6d2add1c49b7c44447e3e78
LIBRETRO_SAME_CDI_SITE = $(call github,libretro,same_cdi,$(LIBRETRO_SAME_CDI_VERSION))
LIBRETRO_SAME_CDI_LICENSE = GPL-2.0+
LIBRETRO_SAME_CDI_LICENSE_FILES = COPYING
# Patches (beside this file, applied by Buildroot in order; each says where it comes from):
# - 0001-gcc13-fix.patch (ROCKNIX's 001-gcc13-fix.patch): a missing <cstdint>

define LIBRETRO_SAME_CDI_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) -f Makefile.libretro platform=unix CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" AR="$(TARGET_AR)"
endef

define LIBRETRO_SAME_CDI_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/same_cdi_libretro.so $(TARGET_DIR)/usr/lib/libretro/same_cdi_libretro.so
endef

$(eval $(generic-package))

################################################################################
#
# libretro-bananagba
#
################################################################################

# BananaGBA: BananaOS's mGBA (libretro's mGBA fork + the handhelds' changes: the threaded renderer, workload
# counters; ~/banana-emu/BananaGBA, branch banana/main). The same CMake build as libretro-mgba; installed beside it as
# bananagba_libretro.so (Settings > Other systems > Game Boy Advance > BananaGBA; BananaFront or RetroArch run it).
# The repository JorreFog/BananaGBA is created at the release step (until then: a local build).
LIBRETRO_BANANAGBA_VERSION = 05efdf9fdd9dbf36378a759ca828f5941ba87690
LIBRETRO_BANANAGBA_SITE = $(call github,JorreFog,BananaGBA,$(LIBRETRO_BANANAGBA_VERSION))
LIBRETRO_BANANAGBA_LICENSE = MPL-2.0
LIBRETRO_BANANAGBA_LICENSE_FILES = LICENSE
# the threaded renderer on by default (-21 % on the emulator's core, measured on gbastress), link-time optimisation
# (-1.4 %; gcc-ar/gcc-ranlib so the LTO objects link)
LIBRETRO_BANANAGBA_CONF_OPTS = -DLIBMGBA_ONLY=ON -DBUILD_LIBRETRO=ON -DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_C_FLAGS="$(TARGET_CFLAGS) -DBANANA_THREADED_VIDEO_DEFAULT=1 -flto=auto -fno-fat-lto-objects" \
	-DCMAKE_SHARED_LINKER_FLAGS="$(TARGET_LDFLAGS) -flto=auto" \
	-DCMAKE_AR=$(TARGET_CROSS)gcc-ar -DCMAKE_RANLIB=$(TARGET_CROSS)gcc-ranlib

define LIBRETRO_BANANAGBA_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $$(find $(@D) -name mgba_libretro.so | head -1) $(TARGET_DIR)/usr/lib/libretro/bananagba_libretro.so
endef

$(eval $(cmake-package))

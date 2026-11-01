################################################################################
#
# libretro-bananagba
#
################################################################################

# BananaGBA: BananaOS's mGBA (libretro's mGBA fork + the handhelds' changes: the threaded renderer, workload
# counters; ~/banana-emu/BananaGBA, branch banana/main). The same CMake build as libretro-mgba; installed beside it as
# bananagba_libretro.so (Settings > Other systems > Game Boy Advance > BananaGBA; BananaFront or RetroArch run it).
# The repository JorreFog/BananaGBA is created at the release step (until then: a local build).
LIBRETRO_BANANAGBA_VERSION = d6c1aac631c38b3e1ddc37d31beb9e86a3204520
LIBRETRO_BANANAGBA_SITE = $(call github,JorreFog,BananaGBA,$(LIBRETRO_BANANAGBA_VERSION))
LIBRETRO_BANANAGBA_LICENSE = MPL-2.0
LIBRETRO_BANANAGBA_LICENSE_FILES = LICENSE
# the threaded renderer on "auto" (on in BananaFront: the cluster runs at half the clock; off in RetroArch, whose GBA
# profile holds ~1.45 GHz where the thread costs CPU), link-time optimisation
# (-1.4 %; gcc-ar/gcc-ranlib so the LTO objects link) and profile-guided optimisation (-6.7 % more): the profile
# (banana/pgo, .gcda files trained on the RG DS with gbastress, same gcc 14) goes next to the objects of this in-source
# CMake build before it compiles; a file that doesn't match its source is ignored (-Wno-coverage-mismatch)
LIBRETRO_BANANAGBA_PGO_FLAGS = -fprofile-use -fprofile-partial-training -Wno-missing-profile -Wno-coverage-mismatch
define LIBRETRO_BANANAGBA_PGO_PROFILE
	if [ -d $(@D)/banana/pgo/CMakeFiles ]; then cp -a $(@D)/banana/pgo/CMakeFiles $(@D)/; fi
endef
LIBRETRO_BANANAGBA_PRE_CONFIGURE_HOOKS += LIBRETRO_BANANAGBA_PGO_PROFILE
LIBRETRO_BANANAGBA_CONF_OPTS = -DLIBMGBA_ONLY=ON -DBUILD_LIBRETRO=ON -DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_C_FLAGS="$(TARGET_CFLAGS) -DBANANA_THREADED_VIDEO_DEFAULT=2 -flto=auto -fno-fat-lto-objects $(LIBRETRO_BANANAGBA_PGO_FLAGS)" \
	-DCMAKE_SHARED_LINKER_FLAGS="$(TARGET_LDFLAGS) -flto=auto -fprofile-use" \
	-DCMAKE_AR=$(TARGET_CROSS)gcc-ar -DCMAKE_RANLIB=$(TARGET_CROSS)gcc-ranlib

define LIBRETRO_BANANAGBA_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $$(find $(@D) -name mgba_libretro.so | head -1) $(TARGET_DIR)/usr/lib/libretro/bananagba_libretro.so
endef

$(eval $(cmake-package))

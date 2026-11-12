# SPDX-License-Identifier: MPL-2.0
# BananaGBA: BananaOS's mGBA (libretro's mGBA fork with the handhelds' changes: the threaded renderer, workload
# counters), as a RetroArch core beside ROCKNIX's mgba (bananagba_libretro.so; es_systems.cfg's gba list by
# tools/rocknix-banana.sh). The pin is BananaLinux's buildroot/package/libretro-bananagba's. Its profile-guided
# optimisation was trained on the RG DS's Cortex-A55 with another GCC: not used here.

PKG_NAME="bananagba-lr"
PKG_VERSION="d6c1aac631c38b3e1ddc37d31beb9e86a3204520"
PKG_LICENSE="MPL-2.0"
PKG_SITE="https://github.com/JorreFog/BananaGBA"
PKG_URL="${PKG_SITE}/archive/${PKG_VERSION}.tar.gz"
PKG_DEPENDS_TARGET="toolchain retroarch"
PKG_LONGDESC="BananaGBA - BananaOS's Game Boy Advance core (mGBA)"
PKG_TOOLCHAIN="cmake"

# (the threaded renderer on "auto": on where the frontend asks for it)
PKG_CMAKE_OPTS_TARGET="-DLIBMGBA_ONLY=ON -DBUILD_LIBRETRO=ON -DCMAKE_BUILD_TYPE=Release \
                       -DCMAKE_C_FLAGS='${CFLAGS} -DBANANA_THREADED_VIDEO_DEFAULT=2'"

makeinstall_target() {
  mkdir -p ${INSTALL}/usr/lib/libretro
    cp $(find ${PKG_BUILD} -name mgba_libretro.so | head -1) ${INSTALL}/usr/lib/libretro/bananagba_libretro.so
}

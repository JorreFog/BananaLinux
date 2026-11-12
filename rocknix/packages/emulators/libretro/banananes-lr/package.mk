# SPDX-License-Identifier: GPL-2.0
# BananaNES: BananaOS's NES core (a fork of libretro's FCEUmm tuned for the handhelds), as a RetroArch core beside
# ROCKNIX's fceumm (banananes_libretro.so; es_systems.cfg's nes list by tools/rocknix-banana.sh). The pin is
# BananaLinux's buildroot/package/libretro-banananes's. Built with its own banana/Makefile in its Buildroot mode, for
# this CPU and without the Cortex-A55's training profile.

PKG_NAME="banananes-lr"
PKG_VERSION="314422eb4d52f054d4ffce39f3bfac87e8502bcc"
PKG_LICENSE="GPL-2.0"
PKG_SITE="https://github.com/JorreFog/BananaNES"
PKG_URL="${PKG_SITE}/archive/${PKG_VERSION}.tar.gz"
PKG_DEPENDS_TARGET="toolchain retroarch"
PKG_LONGDESC="BananaNES - BananaOS's NES core (FCEUmm)"
PKG_TOOLCHAIN="manual"

make_target() {
  make -C ${PKG_BUILD}/banana BUILDROOT=1 NO_PGO=1 CC="${CC}" AR="${AR}" \
    ARCH_FLAGS="-mcpu=${TARGET_CPU}" GIT_VERSION=${PKG_VERSION}
}

makeinstall_target() {
  mkdir -p ${INSTALL}/usr/lib/libretro
    cp $(ls ${PKG_BUILD}/build/*/banananes_libretro.so | head -1) ${INSTALL}/usr/lib/libretro/
}

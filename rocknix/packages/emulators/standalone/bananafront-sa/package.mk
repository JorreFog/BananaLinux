# SPDX-License-Identifier: GPL-3.0-or-later
# BananaFront: BananaOS's own libretro frontend (the low-latency one, its own clock governor, the in-game menu), as a
# ROCKNIX standalone emulator for the systems BananaOS runs it for (es_systems.cfg's lists by BananaLinux's
# tools/rocknix-register.py: cores named bananafront-<libretro core>, runemu.sh runs start_bananafront.sh). Under
# ROCKNIX's sway it draws in an SDL window (patches/0001, make sdl); the cores are ROCKNIX's (and Banana's) libretro
# cores in /usr/lib/libretro. The pin is BananaLinux's buildroot/package/bananafront's.

PKG_NAME="bananafront-sa"
PKG_VERSION="e6b8e47d04d3ef6fc3f769c3b988bf3b70055f3b"
PKG_LICENSE="GPL-3.0-or-later"
PKG_SITE="https://github.com/JorreFog/BananaFront"
PKG_URL="${PKG_SITE}/archive/${PKG_VERSION}.tar.gz"
PKG_DEPENDS_TARGET="toolchain SDL2 alsa-lib zlib"
PKG_LONGDESC="BananaFront - BananaOS's libretro frontend"
PKG_TOOLCHAIN="manual"

make_target() {
  make -C ${PKG_BUILD} sdl CC="${CC}" AR="${AR}" PKG_CONFIG="${PKG_CONFIG:-pkg-config}" \
    CFLAGS="${CFLAGS} -mcpu=${TARGET_CPU}" LDFLAGS="${LDFLAGS}" VERSION=${PKG_VERSION}
}

makeinstall_target() {
  mkdir -p ${INSTALL}/usr/bin
    cp ${PKG_BUILD}/build/sdl/bananafront ${INSTALL}/usr/bin/
    cp ${PKG_DIR}/scripts/start_bananafront.sh ${INSTALL}/usr/bin/
    chmod 0755 ${INSTALL}/usr/bin/bananafront ${INSTALL}/usr/bin/start_bananafront.sh
}

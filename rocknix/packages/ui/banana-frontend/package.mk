# SPDX-License-Identifier: MIT
# BananaOS's menu as ROCKNIX's frontend on dual-screen handhelds (the AYN Thor first): banana-ui on both screens as
# two windows under sway, in place of EmulationStation and lowerdeck; games through ROCKNIX's own runemu.sh.
# Installed into a ROCKNIX checkout by BananaLinux's tools/rocknix-banana.sh, which also puts banana-ui's source
# beside this file (sources/ui) and adds this package to the device's ADDITIONAL_PACKAGES.

PKG_NAME="banana-frontend"
PKG_VERSION="local"
PKG_LICENSE="MIT"
PKG_SITE="https://github.com/JorreFog/BananaLinux"
PKG_URL=""
PKG_DEPENDS_TARGET="toolchain SDL2 freetype libdrm zlib alsa-lib Python3"
PKG_LONGDESC="BananaOS's dual-screen menu (banana-ui) as the frontend, under sway"
PKG_TOOLCHAIN="manual"

unpack() {
  mkdir -p ${PKG_BUILD}
  cp -a ${PKG_DIR}/sources/ui ${PKG_BUILD}/
}

make_target() {
  # the display through SDL2 (Wayland: one window per screen) besides KMS, the menu's sounds through ALSA (PipeWire's
  # default device on ROCKNIX)
  make -C ${PKG_BUILD}/ui banana-ui CXX="${CXX}" PKG_CONFIG="${PKG_CONFIG:-pkg-config}" \
    CXXFLAGS="${CXXFLAGS} -O2" LDFLAGS="${LDFLAGS}" SDL=1 ALSA=1
}

makeinstall_target() {
  mkdir -p ${INSTALL}/usr/bin ${INSTALL}/usr/share/banana
    cp ${PKG_BUILD}/ui/banana-ui ${INSTALL}/usr/bin/
    cp ${PKG_DIR}/sources/start_banana.sh ${INSTALL}/usr/bin/
    chmod 0755 ${INSTALL}/usr/bin/banana-ui ${INSTALL}/usr/bin/start_banana.sh
  for d in rnds splash banana lang speaker-eq; do
    mkdir -p ${INSTALL}/usr/share/banana/${d}
    cp -a ${PKG_BUILD}/ui/assets/${d}/. ${INSTALL}/usr/share/banana/${d}/
  done
  # the shelf's Files tile (one of the menu's own pages)
  mkdir -p ${INSTALL}/usr/share/banana/apps/files
    touch ${INSTALL}/usr/share/banana/apps/files/Files.app
  mkdir -p ${INSTALL}/usr/share/banana/rocknix
    cp ${PKG_DIR}/sources/es-systems-to-banana.py ${INSTALL}/usr/share/banana/rocknix/
}

post_install() {
  enable_service bananasway.service
}

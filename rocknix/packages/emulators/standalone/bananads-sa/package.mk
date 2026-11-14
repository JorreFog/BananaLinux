# SPDX-License-Identifier: GPL-3.0-or-later
# BananaDS: BananaOS's DS emulator on melonDS's core (it lives in the SuperDrastic repository under bananads/), as a
# ROCKNIX standalone emulator of the nds system (es_systems.cfg's list by BananaLinux's tools/rocknix-register.py;
# runemu.sh runs start_bananads.sh). Under ROCKNIX's sway it draws through its SDL frontend, a full-screen window on
# each screen (patches/0001); the RG DS's KMS frontend (panels, in-game menu, companion) needs DRM master, which sway
# holds. The pin is BananaLinux's buildroot/package/bananads's.

PKG_NAME="bananads-sa"
PKG_VERSION="5ecd2f7be5014dfbd7e134b6feed11d956c3adfc"
PKG_LICENSE="GPL-3.0-or-later"
PKG_SITE="https://github.com/JorreFog/SuperDrastic"
PKG_URL="${PKG_SITE}/archive/${PKG_VERSION}.tar.gz"
# melonDS: the commit BananaDS pins is ROCKNIX's melonds-sa's own, so its source tarball is unpacked again here,
# pristine, with BananaDS's patches to it (bananads/patches/melonds) on top
PKG_DEPENDS_TARGET="toolchain SDL2 zlib"
PKG_DEPENDS_UNPACK="melonds-sa"
PKG_LONGDESC="BananaDS - BananaOS's Nintendo DS emulator (melonDS's core)"
PKG_TOOLCHAIN="cmake"
PKG_CMAKE_SCRIPT="${PKG_BUILD}/bananads/CMakeLists.txt"

pre_configure_target() {
  rm -rf ${PKG_BUILD}/melonds
  mkdir -p ${PKG_BUILD}/melonds
  tar -xf ${SOURCES}/melonds-sa/melonds-sa-$(get_pkg_version melonds-sa).tar.gz --strip-components=1 -C ${PKG_BUILD}/melonds
  for p in ${PKG_BUILD}/bananads/patches/melonds/*.patch; do
    patch -d ${PKG_BUILD}/melonds -p1 < "${p}"
  done
  # the Thor's big cores; melonDS's JIT; SDL for the window, sound and pads (no ALSA, no KMS under sway)
  PKG_CMAKE_OPTS_TARGET="-DBANANADS_MELONDS_DIR=${PKG_BUILD}/melonds \
                         -DBANANADS_MELONDS_PATCHES=OFF \
                         -DBANANADS_JIT=melonds \
                         -DBANANADS_SDL=ON \
                         -DBANANADS_ALSA=OFF \
                         -DBANANADS_KMS=OFF \
                         -DBANANADS_LAN=OFF \
                         -DBANANADS_CPU=${TARGET_CPU} \
                         -DBANANADS_LTO=ON \
                         -DCMAKE_BUILD_TYPE=Release"
}

make_target() {
  ninja ${NINJA_OPTS} bananads
}

makeinstall_target() {
  mkdir -p ${INSTALL}/usr/bin
    cp ${PKG_BUILD}/.${TARGET_NAME}/bananads ${INSTALL}/usr/bin/
    cp ${PKG_DIR}/scripts/start_bananads.sh ${INSTALL}/usr/bin/
    chmod 0755 ${INSTALL}/usr/bin/bananads ${INSTALL}/usr/bin/start_bananads.sh
}

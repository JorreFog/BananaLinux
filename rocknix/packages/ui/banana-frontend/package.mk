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
PKG_DEPENDS_TARGET="toolchain SDL2 freetype libdrm zlib alsa-lib Python3 curl"
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
  # box art and game names from libretro's collections (Settings > System > Box art; libcurl opened at run time)
  make -C ${PKG_BUILD}/ui banana-scrape CXX="${CXX}" CXXFLAGS="${CXXFLAGS} -O2" LDFLAGS="${LDFLAGS}"
}

makeinstall_target() {
  mkdir -p ${INSTALL}/usr/bin ${INSTALL}/usr/share/banana
    cp ${PKG_BUILD}/ui/banana-ui ${PKG_BUILD}/ui/banana-scrape ${INSTALL}/usr/bin/
    cp ${PKG_DIR}/sources/start_banana.sh ${INSTALL}/usr/bin/
    chmod 0755 ${INSTALL}/usr/bin/banana-ui ${INSTALL}/usr/bin/banana-scrape ${INSTALL}/usr/bin/start_banana.sh
  for d in rnds splash banana lang speaker-eq; do
    mkdir -p ${INSTALL}/usr/share/banana/${d}
    cp -a ${PKG_BUILD}/ui/assets/${d}/. ${INSTALL}/usr/share/banana/${d}/
  done
  # the shelf's Files tile (one of the menu's own pages)
  mkdir -p ${INSTALL}/usr/share/banana/apps/files
    touch ${INSTALL}/usr/share/banana/apps/files/Files.app
  mkdir -p ${INSTALL}/usr/share/banana/rocknix
    cp ${PKG_DIR}/sources/es-systems-to-banana.py ${INSTALL}/usr/share/banana/rocknix/
  # BananaOS's system scripts the menu runs (Wi-Fi, Bluetooth, volume, updates, SSH, sharing, the DS's saves...), in
  # their services mode (HW_SERVICES=systemd in this hardware profile: ROCKNIX's units and settings underneath)
  mkdir -p ${INSTALL}/usr/libexec/banana ${INSTALL}/etc/banana/sleep.d
    cp -a ${PKG_DIR}/sources/libexec/. ${INSTALL}/usr/libexec/banana/
    cp -a ${PKG_DIR}/sources/sleep.d/. ${INSTALL}/etc/banana/sleep.d/
    cp ${PKG_DIR}/sources/hw.conf ${INSTALL}/etc/banana/hw.conf
    echo rocknix > ${INSTALL}/etc/banana/board
  # the frontend choice (ROCKNIX's autostart, common scripts) and its default for a fresh /storage
  mkdir -p ${INSTALL}/usr/lib/autostart/common ${INSTALL}/usr/config/profile.d
    cp ${PKG_DIR}/sources/099-banana-frontend ${INSTALL}/usr/lib/autostart/common/
    chmod 0755 ${INSTALL}/usr/lib/autostart/common/099-banana-frontend
    echo 'UI_SERVICE="sway.service bananasway.service"' > ${INSTALL}/usr/config/profile.d/099-banana-ui_service
  # the paths BananaOS's scripts and the menu know, on ROCKNIX's writable /storage
  mkdir -p ${INSTALL}/data
    ln -sf /storage/.config/banana ${INSTALL}/data/banana
    ln -sf /storage/roms ${INSTALL}/data/roms
    ln -sf /storage/roms/bios ${INSTALL}/data/bios
}

# (started by ROCKNIX's autostart as UI_SERVICE, as EmulationStation's essway is: the common start script chooses it,
# ROCKNIX's banana.frontend setting going back to EmulationStation)

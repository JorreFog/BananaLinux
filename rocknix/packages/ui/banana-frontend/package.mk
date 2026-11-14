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
# (grim: the quick menu slides over a picture of what a two-screen game shows on the bottom output)
PKG_DEPENDS_TARGET="toolchain SDL2 freetype libdrm zlib alsa-lib Python3 curl openssl libssh2 grim"
PKG_LONGDESC="BananaOS's dual-screen menu (banana-ui) as the frontend, under sway"
PKG_TOOLCHAIN="manual"

unpack() {
  mkdir -p ${PKG_BUILD}
  cp -a ${PKG_DIR}/sources/ui ${PKG_DIR}/sources/activity ${PKG_BUILD}/
}

make_target() {
  # the display through SDL2 (Wayland: one window per screen) besides KMS, the menu's sounds through ALSA (PipeWire's
  # default device on ROCKNIX)
  make -C ${PKG_BUILD}/ui banana-ui CXX="${CXX}" PKG_CONFIG="${PKG_CONFIG:-pkg-config}" \
    CXXFLAGS="${CXXFLAGS} -O2" LDFLAGS="${LDFLAGS}" SDL=1 ALSA=1
  # its helpers: box art and game names (banana-scrape: libretro's collections, libcurl opened at run time), home
  # server sync (banana-sync), Bshare (banana-bshare: LocalSend), Files' servers without SMB (banana-fetch SMB=0)
  make -C ${PKG_BUILD}/ui banana-scrape banana-sync banana-bshare CXX="${CXX}" PKG_CONFIG="${PKG_CONFIG:-pkg-config}" \
    CXXFLAGS="${CXXFLAGS} -O2" LDFLAGS="${LDFLAGS}"
  make -C ${PKG_BUILD}/ui banana-fetch SMB=0 CXX="${CXX}" PKG_CONFIG="${PKG_CONFIG:-pkg-config}" \
    CXXFLAGS="${CXXFLAGS} -O2" LDFLAGS="${LDFLAGS}"
  # Activity (store/apps/activity): its SDL windows under sway (SDL opened at run time)
  ${CXX} ${CXXFLAGS} -O2 -std=c++17 ${LDFLAGS} -o ${PKG_BUILD}/banana-activity ${PKG_BUILD}/activity/banana-activity.cpp -ldl
}

makeinstall_target() {
  mkdir -p ${INSTALL}/usr/bin ${INSTALL}/usr/share/banana
    cp ${PKG_BUILD}/ui/banana-ui ${INSTALL}/usr/bin/
    for b in banana-scrape banana-sync banana-bshare banana-fetch; do cp ${PKG_BUILD}/ui/${b} ${INSTALL}/usr/bin/; done
    cp ${PKG_BUILD}/banana-activity ${INSTALL}/usr/bin/
    cp ${PKG_DIR}/sources/start_banana.sh ${PKG_DIR}/sources/bin/banana-perf ${INSTALL}/usr/bin/
    chmod 0755 ${INSTALL}/usr/bin/banana-* ${INSTALL}/usr/bin/start_banana.sh
  for d in rnds splash banana lang speaker-eq; do
    mkdir -p ${INSTALL}/usr/share/banana/${d}
    cp -a ${PKG_BUILD}/ui/assets/${d}/. ${INSTALL}/usr/share/banana/${d}/
  done
  # the themes (Thor OLED, the AYN Thor's default, with its White, Clear Purple and Rainbow; the other examples) and the
  # companions' game data
  mkdir -p ${INSTALL}/usr/share/banana/themes ${INSTALL}/usr/share/banana/companions
    cp -a ${PKG_DIR}/sources/themes/. ${INSTALL}/usr/share/banana/themes/
    cp -a ${PKG_DIR}/sources/companions/. ${INSTALL}/usr/share/banana/companions/
  # the home row's own tiles: Files and Bshare (the menu's own pages), Activity
  mkdir -p ${INSTALL}/usr/share/banana/apps/files ${INSTALL}/usr/share/banana/apps/bshare ${INSTALL}/usr/share/banana/apps/activity
    touch ${INSTALL}/usr/share/banana/apps/files/Files.app ${INSTALL}/usr/share/banana/apps/bshare/Bshare.app \
      ${INSTALL}/usr/share/banana/apps/activity/Activity.app
  mkdir -p ${INSTALL}/usr/share/banana/rocknix
    cp ${PKG_DIR}/sources/es-systems-to-banana.py ${PKG_DIR}/sources/hw.conf ${PKG_DIR}/sources/hw-*.conf ${INSTALL}/usr/share/banana/rocknix/
  # BananaOS's system scripts the menu runs (Wi-Fi, Bluetooth, volume, updates, SSH, sharing, the DS's saves...), in
  # their services mode (HW_SERVICES=systemd in this hardware profile: ROCKNIX's units and settings underneath)
  mkdir -p ${INSTALL}/usr/libexec/banana ${INSTALL}/etc/banana/sleep.d
    cp -a ${PKG_DIR}/sources/libexec/. ${INSTALL}/usr/libexec/banana/
    cp ${PKG_DIR}/sources/rocknix-device ${PKG_DIR}/sources/rocknix-frontend ${INSTALL}/usr/libexec/banana/
    chmod 0755 ${INSTALL}/usr/libexec/banana/*
    cp -a ${PKG_DIR}/sources/sleep.d/. ${INSTALL}/etc/banana/sleep.d/
  # the board and the hardware profile are the handheld's (one SM8550 image serves several): rocknix-device writes them
  # into /run/banana at boot and at the menu's start (board thor on the AYN Thor: the menu's Thor defaults)
    ln -sf /run/banana/hw.conf ${INSTALL}/etc/banana/hw.conf
    ln -sf /run/banana/board ${INSTALL}/etc/banana/board
  # the frontend choice (ROCKNIX's autostart, common scripts) and its default for a fresh /storage
  mkdir -p ${INSTALL}/usr/lib/autostart/common ${INSTALL}/usr/config/profile.d
    cp ${PKG_DIR}/sources/099-banana-frontend ${INSTALL}/usr/lib/autostart/common/
    chmod 0755 ${INSTALL}/usr/lib/autostart/common/099-banana-frontend
    echo 'UI_SERVICE="sway.service bananasway.service"' > ${INSTALL}/usr/config/profile.d/099-banana-ui_service
  # the paths BananaOS's scripts, the menu and its apps know (/data/banana, /data/roms, /data/store...), on ROCKNIX's
  # writable /storage: /data is a folder there, its roms and bios ROCKNIX's own (start_banana.sh makes them)
  ln -sf /storage/.config/banana-data ${INSTALL}/data
}

# (started by ROCKNIX's autostart as UI_SERVICE, as EmulationStation's essway is: the common start script chooses it,
# ROCKNIX's banana.frontend setting going back to EmulationStation)

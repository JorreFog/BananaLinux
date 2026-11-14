# SPDX-License-Identifier: MIT
# BananaOS's apps on the menu's home row, for ROCKNIX: the Store (Store apps, Bank & Trade come from it) and Döda
# Kvarter, both from the ROCKNIXDS repository at BananaLinux's pin (buildroot/package/rocknixds-store, dodakvarter),
# with BananaOS's Store patches (tools/rocknix-banana.sh copies buildroot/patches/rocknixds-store here as patches/
# 0001-0003) and the two-window SDL mode for sway (patches/0010). The menu starts them through its run-store and
# run-dodakvarter, which take the SDL route under sway. Their tiles' art and the Store's catalog: sources/ (copied in
# by tools/rocknix-banana.sh from buildroot/package and store/).

PKG_NAME="banana-apps"
PKG_VERSION="ed9e3b543e6891ec4d75ceb490f73d37ebe84002"
PKG_LICENSE="MIT"
PKG_SITE="https://github.com/JorreFog/ROCKNIXDS"
PKG_URL="${PKG_SITE}/archive/${PKG_VERSION}.tar.gz"
PKG_DEPENDS_TARGET="toolchain SDL2 libdrm"
PKG_LONGDESC="BananaOS's Store and Döda Kvarter"
PKG_TOOLCHAIN="manual"

DK_STORE_SRC="plat.c plat_kms.c plat_sdl.c plat_headless.c input_evdev.c audio_alsa.c gfx.c font.c png.c"
DK_SRC="main.c game.c mapgen.c world.c props.c render.c hud.c menu.c update.c radio.c music.c weapons.c zombies.c loot.c
        inter.c audio.c save.c lang.c data.c bot.c art.c art_data.c gfx.c font.c png.c plat.c plat_headless.c plat_kms.c
        plat_sdl.c selftest.c input_evdev.c audio_alsa.c boss.c splash.c"

make_target() {
  # (SDL2's headers only: SDL is opened at run time)
  local inc="-I${SYSROOT_PREFIX}/usr/include/libdrm -I${SYSROOT_PREFIX}/usr/include/SDL2"
  cd ${PKG_BUILD}/store
  ${CC} ${CFLAGS} -O2 -w -DBANANA -DSTORE_VERSION="\"$(cat VERSION)-banana\"" -I../dodakvarter/src ${inc} \
    -o store src/main.c src/job.c src/catalog.c $(for f in ${DK_STORE_SRC}; do echo ../dodakvarter/src/${f}; done) \
    ${LDFLAGS} -ldrm -lm -lpthread -ldl
  cd ${PKG_BUILD}/dodakvarter
  ${CC} ${CFLAGS} -O2 -ffp-contract=off -w -DDK_VERSION="\"$(cat VERSION)\"" ${inc} \
    -o dodakvarter $(for f in ${DK_SRC}; do echo src/${f}; done) ${LDFLAGS} -ldrm -lm -lpthread -ldl
}

makeinstall_target() {
  local A=${INSTALL}/usr/share/banana/apps
  mkdir -p ${INSTALL}/usr/bin ${A}/store/media ${A}/dodakvarter/media
    cp ${PKG_BUILD}/store/store ${INSTALL}/usr/bin/rocknixds-store-ui
    cp ${PKG_BUILD}/dodakvarter/dodakvarter ${INSTALL}/usr/bin/
    chmod 0755 ${INSTALL}/usr/bin/rocknixds-store-ui ${INSTALL}/usr/bin/dodakvarter
  # the Store: its package manager, catalog, the apps' tiles
    cp ${PKG_BUILD}/store/device/rocknixds-store ${A}/store/
    chmod 0755 ${A}/store/rocknixds-store
    cp ${PKG_BUILD}/store/VERSION ${A}/store/
    cp ${PKG_DIR}/sources/store/catalog.json ${A}/store/
    cp -a ${PKG_DIR}/sources/store/apps ${A}/store/ 2>/dev/null || :
    cp ${PKG_DIR}/sources/store/store-image.png ${A}/store/media/
    cp ${PKG_DIR}/sources/store/gamelist.xml ${A}/store/
    touch "${A}/store/Store.app"
  # Döda Kvarter
    cp ${PKG_BUILD}/dodakvarter/VERSION ${A}/dodakvarter/
    cp ${PKG_BUILD}/dodakvarter/device/media/*.png ${A}/dodakvarter/media/
    cp ${PKG_DIR}/sources/dodakvarter/gamelist.xml ${A}/dodakvarter/
    touch "${A}/dodakvarter/Döda Kvarter.app"
}

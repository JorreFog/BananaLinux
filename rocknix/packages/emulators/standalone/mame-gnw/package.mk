# SPDX-License-Identifier: GPL-2.0-or-later
# Game & Watch through MAME: BananaOS's mame-gnw (MAME's hh_sm510 driver only, every Nintendo Game & Watch) as a
# ROCKNIX standalone emulator of the gameandwatch system (es_systems.cfg's list by BananaLinux's
# tools/rocknix-register.py; runemu.sh runs start_mamegnw.sh). The pin and BananaOS's patches are
# buildroot/package/mame-gnw's (tools/rocknix-banana.sh copies buildroot/patches/mame-gnw here as patches/). Under
# ROCKNIX's sway it draws through SDL on Wayland: a two-screen game is two windows, one on each screen.

PKG_NAME="mame-gnw"
PKG_VERSION="mame0289"
PKG_LICENSE="GPL-2.0-or-later"
PKG_SITE="https://github.com/mamedev/mame"
PKG_URL="${PKG_SITE}/archive/${PKG_VERSION}.tar.gz"
PKG_DEPENDS_TARGET="toolchain SDL2 SDL2_ttf Python3:host"
PKG_LONGDESC="MAME's Game & Watch driver (hh_sm510), BananaOS's build"
PKG_TOOLCHAIN="manual"

make_target() {
  # MAME's own makefile, cross-compiled (its build tools with the host's compiler): SDL2 from the sysroot, no X11, no
  # desktop OpenGL (SDL's GLES renderer draws), no Qt debugger, no fontconfig (patch 0001); at most 6 compilers at a
  # time (some of MAME's files take 1-2 GB each)
  local jobs=$(nproc); [ ${jobs} -gt 6 ] && jobs=6
  make -j${jobs} -C ${PKG_BUILD} \
    SUBTARGET=gnw SOURCES=src/mame/handheld/hh_sm510.cpp \
    TARGETOS=linux CROSS_BUILD=1 PLATFORM=arm64 \
    OVERRIDE_CC="${CC}" OVERRIDE_CXX="${CXX}" OVERRIDE_AR="${AR}" \
    SDL_INSTALL_ROOT=${SYSROOT_PREFIX}/usr PYTHON_EXECUTABLE=${TOOLCHAIN}/bin/python3 \
    NO_X11=1 NO_USE_XINPUT=1 NO_OPENGL=1 USE_QTDEBUG=0 NO_USE_MIDI=1 NO_USE_PORTAUDIO=1 \
    NO_USE_PULSEAUDIO=1 NO_USE_PIPEWIRE=1 TOOLS=0 NOWERROR=1 OPTIMIZE=2 VERBOSE=0 \
    ARCHOPTS="-DMAME_NO_FONTCONFIG -DUSE_OZONE -mcpu=${TARGET_CPU}"
}

makeinstall_target() {
  mkdir -p ${INSTALL}/usr/bin
    cp ${PKG_BUILD}/gnw ${INSTALL}/usr/bin/mame-gnw
    cp ${PKG_DIR}/scripts/start_mamegnw.sh ${INSTALL}/usr/bin/
    chmod 0755 ${INSTALL}/usr/bin/mame-gnw ${INSTALL}/usr/bin/start_mamegnw.sh
}

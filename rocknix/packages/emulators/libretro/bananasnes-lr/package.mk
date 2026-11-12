# SPDX-License-Identifier: GPL-2.0
# BananaSNES: BananaOS's Snes9x fork for the handhelds, as a RetroArch core beside ROCKNIX's snes9x
# (bananasnes_libretro.so; es_systems.cfg's snes list by tools/rocknix-banana.sh). The pin is BananaLinux's
# buildroot/package/libretro-bananasnes's. Snes9x's licence: free distribution only. Its profile-guided optimisation
# was trained on the RG DS's Cortex-A55 with another GCC: not used here.

PKG_NAME="bananasnes-lr"
PKG_VERSION="ee504367e2ea2f19474c4e7d6f79b2404418803f"
PKG_LICENSE="Non-commercial"
PKG_SITE="https://github.com/JorreFog/BananaSNES"
PKG_URL="${PKG_SITE}/archive/${PKG_VERSION}.tar.gz"
PKG_DEPENDS_TARGET="toolchain retroarch"
PKG_LONGDESC="BananaSNES - BananaOS's Super Nintendo core (Snes9x)"
PKG_TOOLCHAIN="make"

PKG_MAKE_OPTS_TARGET="-C libretro platform=unix"

makeinstall_target() {
  mkdir -p ${INSTALL}/usr/lib/libretro
    cp -a libretro/snes9x_libretro.so ${INSTALL}/usr/lib/libretro/bananasnes_libretro.so
}

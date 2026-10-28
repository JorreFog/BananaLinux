# Third-party parts

Banana's own code (ui/src, system/, buildroot/package, tools/) is under the MIT license in LICENSE. It also carries:

| What | Where | From | License |
|---|---|---|---|
| ROCKNIXDS Pixel's engine (RndsRaster, RndsUI) and its theme assets (icons, sounds, Pixelify Sans) | ui/rnds, ui/assets/rnds | [ROCKNIXDS](https://github.com/JorreFog/ROCKNIXDS), imported by tools/import-rnds.sh | MIT; Pixelify Sans: SIL OFL 1.1 (fonts/OFL.txt); console icons: BenJelter's Console Icon Pack (see systems.cfg) |
| Inter (the Carts theme's typeface) | ui/assets/banana/fonts | [rsms/inter](https://github.com/rsms/inter) v4.1 | SIL OFL 1.1 (fonts/Inter-LICENSE.txt) |
| The BananaUI banana mark (redrawn as pixel art by tools/make-pixel-banana.py) | ui/assets/banana, the logo and boot animation | [JorreFog/helios](https://github.com/JorreFog/helios) app/src/main/res/drawable-nodpi/banana_mark.png | the same author's |
| stb_image, stb_image_write | ui/third_party | [nothings/stb](https://github.com/nothings/stb) | public domain / MIT |
| The SM4450 hardware layer: kernel patches, the Retroid Pocket Classic device trees, the kernel config, the ADSP/WPSS/ACF/QCA firmware and the audio topology | buildroot/board/retroid-pocket-classic | [ROCKNIX](https://github.com/ROCKNIX/distribution) `next`, imported 2026-10-27 | GPL-2.0 |
| Mesa's freedreno driver, with the Adreno 613 entries Banana adds | the image's Mesa; buildroot/patches/mesa3d | [Mesa](https://gitlab.freedesktop.org/mesa/mesa) | MIT |
| The RPC joypad's RetroArch autoconfig | system/rootfs/usr/share/libretro/autoconfig/udev/Retroid Pocket Classic Gamepad.cfg | Banana's own, from the rsinput driver's mapping | MIT |
| RetroArch's joypad autoconfigs for the pads people plug in (8BitDo, Xbox, PlayStation) | fetched at build time | [libretro/retroarch-joypad-autoconfig](https://github.com/libretro/retroarch-joypad-autoconfig) | per file, mostly GPL-2.0 |

**OpenABL**, the arm-efi bootloader already in the handheld's `abl_a`/`abl_b` slots, is not built from or
redistributed by this repository; the card only carries the kernel, the device tree and `extlinux/extlinux.conf`
it reads.

Downloaded at build time, not kept here: RetroArch (GPL-3.0) and its cores (gpSP, Gambatte, FCEUmm, PCSX ReARMed,
Mupen64Plus-Next, NEStopia, Snes9x, PicoDrive, Genesis Plus GX, bsnes, mGBA, SameBoy, Parallel N64, fake-08,
SwanStation — the libretro core of DuckStation: GPL-3.0-or-later; per core, see each package), libretro's GLSL
shaders (per shader, mostly GPL/public domain), PortMaster (MIT), libudev-zero (ISC), Linux, Buildroot and its
packages.

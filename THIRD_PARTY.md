# Third-party parts

Banana's own code (ui/src, system/, buildroot/package, tools/) is under the MIT license in LICENSE. It also carries:

| What | Where | From | License |
|---|---|---|---|
| ROCKNIXDS Pixel's engine (RndsRaster, RndsUI) and its theme assets (icons, sounds, Pixelify Sans) | ui/rnds, ui/assets/rnds | [ROCKNIXDS](https://github.com/JorreFog/ROCKNIXDS), imported by tools/import-rnds.sh | MIT; Pixelify Sans: SIL OFL 1.1 (fonts/OFL.txt); console icons: BenJelter's Console Icon Pack (see systems.cfg) |
| Inter (the Carts theme's typeface) | ui/assets/banana/fonts | [rsms/inter](https://github.com/rsms/inter) v4.1 | SIL OFL 1.1 (fonts/Inter-LICENSE.txt) |
| Fusion Pixel Font 12px, proportional, zh_hans (the CJK characters of the menu's languages), unmodified | ui/assets/rnds/fonts/fusion-pixel-12px-proportional-zh_hans.ttf | [TakWolf/fusion-pixel-font](https://github.com/TakWolf/fusion-pixel-font) 2026.10.16 | SIL OFL 1.1 (fonts/fusion-pixel-licenses/: its own, and those of Ark Pixel Font, Cubic 11 and Galmuri, which it merges) |
| The Simplified Chinese translation | ui/assets/lang/zh-cn.tsv | by Majirefy | given to BananaOS by its translator |
| The BananaUI banana mark (redrawn as pixel art by tools/make-pixel-banana.py) | ui/assets/banana, the logo and boot animation | [JorreFog/helios](https://github.com/JorreFog/helios) app/src/main/res/drawable-nodpi/banana_mark.png | the same author's |
| DraStic settings and button map for the RG DS | buildroot/board/rgds/drastic.cfg | ROCKNIX drastic-sa (config/RK3566/drastic.cfg.rgds) | GPL-2.0 (ROCKNIX) |
| stb_image, stb_image_write | ui/third_party | [nothings/stb](https://github.com/nothings/stb) | public domain / MIT |
| Kernel patches, RG DS and RG DS Plus device trees, kernel config, U-Boot patches | buildroot/board/rgds | [ROCKNIX](https://github.com/ROCKNIX/distribution), pinned in board/rgds/ROCKNIX | GPL-2.0 |
| Generic DSI panel driver | buildroot/board/rgds/kernel/panel-generic-dsi.c | ROCKNIX (stolen/overlay_server) | GPL-2.0 |
| ROCKNIX joypad driver | buildroot/board/rgds/kernel/rocknix-* | [hackbar/rocknix-joypad](https://github.com/hackbar/rocknix-joypad) | GPL-2.0-or-later |
| RK3568 TF-A (BL31) | buildroot/board/rgds/rkbin | [rockchip-linux/rkbin](https://github.com/rockchip-linux/rkbin) | Rockchip's binary license (rkbin/LICENSE) |
| Nintendo DS cheat files (Action Replay codes, for BananaDS) | buildroot/package/libretro-cheats-nds (tarball) | [libretro/libretro-database](https://github.com/libretro/libretro-database) cht/Nintendo - Nintendo DS at 12c21cf | CC BY-SA 4.0 |

Bshare (`ui/src/bshare.cpp`, `ui/src/settings_bshare.cpp`) is Banana's own code: it speaks the open protocol of
[LocalSend](https://localsend.org), the free and open-source app by Tien Do Nam and its contributors, as documented in
[localsend/protocol](https://github.com/localsend/protocol), so that LocalSend's apps and the handheld can send each
other files. No LocalSend code, name or logo is included; Bshare's icon is Banana's own pixel drawing of the common
share symbol.

RetroArch's joypad map for the RG DS (`buildroot/package/retroarch/retrogame_joypad.cfg`) is ROCKNIX's (GPL-2.0).

Downloaded at build time, not kept here: RetroArch (GPL-3.0) and its cores (gpSP, Gambatte, FCEUmm, PCSX ReARMed and
Mupen64Plus-Next: GPL-2.0; mGBA: MPL-2.0; libretro's GLSL shaders: per shader, mostly GPL/public domain; RGUI's bitmap fonts from libretro/retroarch-assets rgui/font (CC BY 4.0; the Chinese/Japanese ones from PixelMPlus, free licence; the Korean one Galmuri, SIL OFL 1.1); Snes9x and PicoDrive: non-commercial licences), libudev-zero (ISC), for Files' servers libsmb2 (LGPL-2.1+), libssh2 (BSD-3-Clause), libcurl (curl) and 7-Zip's 7zz (LGPL-2.1+ with the unRAR restriction for its RAR codecs; BSD for its LZFSE, Zstandard and xxHash parts, see its DOC/License.txt), Linux, U-Boot, Buildroot and its packages, SuperDrastic (MIT) and DraStic
(proprietary freeware by Exophase, the build ROCKNIX packages; not redistributed by this repository).

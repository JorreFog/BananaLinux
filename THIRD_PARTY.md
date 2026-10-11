# Third-party parts

Banana's own code (ui/src, system/, buildroot/package, tools/) is under the MIT license in LICENSE. It also carries:

| What | Where | From | License |
|---|---|---|---|
| ROCKNIXDS Pixel's engine (RndsRaster, RndsUI) and its theme assets (icons, sounds, Pixelify Sans) | ui/rnds, ui/assets/rnds | [ROCKNIXDS](https://github.com/JorreFog/ROCKNIXDS), imported by tools/import-rnds.sh | MIT; Pixelify Sans: SIL OFL 1.1 (fonts/OFL.txt); console icons: BenJelter's Console Icon Pack (see systems.cfg) |
| Unbounded (Banana's logo is drawn with it) | ui/assets/overrides/OFL-Unbounded.txt | ROCKNIXDS logo/ | SIL OFL 1.1 |
| stb_image, stb_image_write | ui/third_party | [nothings/stb](https://github.com/nothings/stb) | public domain / MIT |
| Kernel patches, RG DS and RG DS Plus device trees, kernel config, U-Boot patches | buildroot/board/rgds | [ROCKNIX](https://github.com/ROCKNIX/distribution), pinned in board/rgds/ROCKNIX | GPL-2.0 |
| Generic DSI panel driver | buildroot/board/rgds/kernel/panel-generic-dsi.c | ROCKNIX (stolen/overlay_server) | GPL-2.0 |
| ROCKNIX joypad driver | buildroot/board/rgds/kernel/rocknix-* | [hackbar/rocknix-joypad](https://github.com/hackbar/rocknix-joypad) | GPL-2.0-or-later |
| RK3568 TF-A (BL31) | buildroot/board/rgds/rkbin | [rockchip-linux/rkbin](https://github.com/rockchip-linux/rkbin) | Rockchip's binary license (rkbin/LICENSE) |

Downloaded at build time, not kept here: Linux, U-Boot, Buildroot and its packages, SuperDrastic (MIT) and DraStic
(proprietary freeware by Exophase, the build ROCKNIX packages; not redistributed by this repository).

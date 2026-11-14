---
name: banana-board-port
description: How a handheld becomes a BananaOS board (the RG DS family, the AYN Thor, the next one). Use when adding or changing a board, touching buildroot/configs, buildroot/board/*, external.mk's board switch, post-build/post-image, the hardware profile (etc/banana/hw/*.conf, hw-env), or any script that hardcodes RG DS hardware (DSI-2, fe5e0000.i2c, backlight0/1, rtw88, rk817, aw88166, panfrost, mmcblk0).
---

# Porting BananaOS to a board

BananaOS is a Buildroot external tree (`buildroot/`) plus a rootfs overlay (`system/rootfs`) plus the menu (`ui/`).
A board is: a defconfig, a `buildroot/board/<dir>` with its kernel and image bits, and a hardware profile. Everything
else is shared, and the rule is **reuse, don't fork**: a board-specific value goes into the profile or a board
switch, never into a copy of a shared script.

## Where a board lives

| What | Where | Notes |
|---|---|---|
| Build entry | `Makefile` `BOARDS = ...` | `make <board>`, `make <target>-<board>` |
| Defconfig | `buildroot/configs/banana_<board>_defconfig` | Generate from `banana_rgds_defconfig` and change only hardware lines (see `banana_thor_defconfig`'s header). CI checks every line survives `make defconfig`. |
| Board name | `BR2_ROOTFS_POST_SCRIPT_ARGS="<board>"` | becomes `/etc/banana/board`, `update`'s package check, `external.mk`'s `BANANA_BOARD` |
| Kernel | `board/<dir>/linux-rocknix.config` + `board/rgds/linux-banana.fragment` + `board/<dir>/linux-<board>.fragment` | Banana's fragment (squashfs root, no VT/fbdev, trimmed net) applies to every board; a board fragment comes after and undoes what it must |
| Kernel patches / DTS | `board/<dir>/linux-patches`, `board/<dir>/dts`, hooks in `buildroot/external.mk` | Patches applied with fuzz in ROCKNIX's order. Guard board-only hooks with `ifeq ($(BANANA_BOARD),...)` |
| Image | `board/<dir>/genimage.cfg.in`, `post-image.sh` | Keep the partition names `BANANA-BOOT`, `banana-root-a/-b` and the order (p1 boot, p2/p3 roots, p4 data made by firstboot): rcS, firstboot and update depend on them |
| Update package | `banana-<board>-<version>.update.tar`: Image, *.dtb, rootfs.squashfs, VERSION, board, sha256sums | add `<board>-*)` to `update list` |
| Hardware profile | `system/rootfs/etc/banana/hw/<board>.conf` -> `/etc/banana/hw.conf` (post-build) -> `usr/libexec/banana/hw-env` (rcS) | exports `BANANA_TOP`, `BANANA_TOUCH`, `BANANA_PANEL_ROTATE`, `BANANA_BACKLIGHT_*`; `HW_MODULES`, `HW_WIFI_MODULE` read directly |

post-build **fails** when the board has no profile: add one with every key, and mark unverified values `(check)`.

## The menu on other panels

`ui/src/panelfit.*`: the menu draws one canvas (`Display::width()/height()`); `canvasFor()` keeps the panels' size
when both are equal and upright (RG DS: identical behaviour), else 4:3 fitted to the top panel (`BANANA_CANVAS=WxH`
overrides). Each panel's `PanelFit` scales, centres, and turns by the connector's "panel orientation" (or
`BANANA_PANEL_ROTATE=top,bottom`). Touch goes through `Display::touchToCanvas`. Test: `make -C ui tests/panelfit-test`.

Render the menu at a new canvas to look at it:
`ui/banana-ui --headless out/1440x1080- --size 1440x1080 --assets ui/assets/rnds --splash ui/assets/splash --banana ui/assets/banana --systems system/rootfs/etc/banana/systems.conf --roms ui/tests/out/fixture/roms --data ui/tests/out/fixture/data --script "home 0; wait 2400; shot home"` (after `make test` made the fixture).

## Checklist for a new board

1. Defconfig from the RG DS's, hardware lines only; `make -C <buildroot> BR2_EXTERNAL=$PWD/buildroot O=/tmp/x banana_<board>_defconfig` and diff every line (CI's check, `.github/workflows/ci.yml`).
2. Board dir: sync script from ROCKNIX (skill `rocknix-sync`), fragment, image templates, post-image.
3. `external.mk`: board-guard every kernel hook; extra firmware files.
4. Profile `etc/banana/hw/<board>.conf`; `update list` pattern; `Makefile` BOARDS; CI defconfig list and shell-parse globs.
5. Grep for RG DS literals in anything the board will run: `grep -rn "DSI-2\|fe5e0000\|backlight[01]\|rtw88\|rk817\|aw88166\|panfrost\|cortex-a55\|/dev/dri/card0" system ui/src buildroot/package`. Each hit either reads the profile or is listed in the board's port doc (`docs/THOR-PORT.md` has the current list).
6. `make test`, `bash ui/tests/update-apply.sh`, `sh -n` on every changed script, `make -C system/bananad CFLAGS="-O2 -Werror"`.

## Don'ts

- Don't change RG DS behaviour as a side effect: the RG DS profile must reproduce the old literals exactly, and
  `canvasFor` must return the panel size for equal upright panels.
- Don't put a board's defaults in C++ when a profile key can carry it.
- `make test` failures in `ota`, `sync` and `systems.history` can be environmental (no DNS, shallow clone); compare
  against a clean worktree of the base commit before blaming a change.

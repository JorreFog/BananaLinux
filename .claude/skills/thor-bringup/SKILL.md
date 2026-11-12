---
name: thor-bringup
description: Bring BananaOS up on a real AYN Thor (SM8550) and turn what the handheld reports into code: OpenABL boot, first shell, hw-probe, fixing etc/banana/hw/thor.conf, panels, touch, pad, sound, Wi-Fi, sleep. Use when someone has a Thor in hand, pastes a hw-probe report, dmesg or banana-ui.log from one, or works on docs/THOR-PORT.md's milestones M0 to M4.
---

# AYN Thor bring-up

The plan and status are in `docs/THOR-PORT.md` (milestones M0 to M5). The board files are in `buildroot/board/thor`,
the profile is `system/rootfs/etc/banana/hw/thor.conf`. Values marked `(check)` there are guesses from ROCKNIX's
device tree; replace them with what the handheld reports, and delete the `(check)`.

## Facts already established (ROCKNIX 6cf94dba)

- Device tree `qcom/qcs8550-ayn-thor.dts` (+ `qcs8550-ayn-common.dtsi`); kernel 7.2 + 71 ROCKNIX patches.
- Top: 1080x1920 AMOLED, ICNA3520, `mdss_dsi1`, 120/60 Hz, DSC, `rotation = <90>`. Bottom: 1080x1240 AMOLED,
  CH13726A, `mdss_dsi0`, 60 Hz, `rotation = <90>`. One msm DRM card, two DSI connectors.
- Touch: FT5426 top on i2c4 (swapped-x-y, inverted-x), FT5452 bottom on `i2c_hub_3`; edt-ft5x06.
- Pad: RSInput MCU on uart15, "AYN Odin2 Gamepad" 2020:3001, sticks ABS_X/Y/RX/RY, triggers ABS_Z/RZ, BTN_MODE,
  BTN_BACK; AYN key KEY_F24 (gpio-keys); lid SW_LID (gpio17).
- Sound: WCD9385 + 2x AW88166 via the ADSP (card "AYN-Thor"); routing in ROCKNIX's UCM patch
  `projects/ROCKNIX/packages/audio/alsa-ucm-conf/patches/SM8550/0005_Add-AYN-Thor.patch`.
- WCN7850: ath12k on PCIe, BT QCA on uart14. Fan pwm-fan. Stick LEDs HTR3212 x2. Haptics on pm8550b.
- Boot: OpenABL v1.2 (flashed to `abl_a`/`abl_b`), EFI-stub kernel, reads BLS entries or extlinux from the card's
  first FAT partition; device tree from the entry or its device-model menu (`/dtbs`).
- ROCKNIX: suspend is "fake suspend" only on SM8550.

## The loop

1. Get a shell: SSH (Settings > Network > File sharing > SSH; public key in `banana/ssh/authorized_keys` on the card)
   over Wi-Fi or a USB Ethernet adapter (rcS runs DHCP on `eth*`).
   Logs: `/data/banana/logs/` (`banana-ui.log`, `boot.txt`, `dmesg.txt`).
2. `/usr/libexec/banana/hw-probe` writes `/data/banana/logs/hw-probe.txt`: connectors and modes, input devices with
   their sysfs paths and capabilities, ALSA cards and controls, power supplies, backlights, LEDs, thermal zones,
   remoteprocs, network drivers, firmware requests, errors, deferred probes. Read it all before changing code.
3. Map the report to the profile:
   - `HW_TOP`: the connector whose mode is 1080x1920 (`/sys/class/drm/card*-DSI-*/modes`).
   - `HW_TOUCH`: a substring of the bottom touchscreen's sysfs path that the top one's lacks (the probe prints `phys`
     and event paths; banana-ui matches against the device's sysfs path).
   - `HW_PANEL_ROTATE`: leave empty when `modetest -c` shows `panel orientation` = `Right Side Up` on both (banana-ui
     turns them 90); set `90,90` / `270,270` only if the picture comes out sideways or upside down.
   - `HW_BACKLIGHT_TOP/BOTTOM`: `/sys/class/backlight/*` names for each panel.
   - `HW_MODULES`, `HW_WIFI_MODULE`: what `lsmod` needs for Wi-Fi; whether Wi-Fi survives sleep.
4. Quick experiments need no rebuild: environment variables override the profile for one run,
   e.g. `killall banana-ui; BANANA_PANEL_ROTATE=270,270 BANANA_TOP=DSI-1 /usr/bin/banana-ui-session`.
   `BANANA_CANVAS=1600x1200` tries another canvas.
5. Turn findings into commits: profile values, then code for what the profile can't express. Add the probe report to
   `docs/thor/` so later work has the real `/sys`.

## Known sharp edges

- The root is read-only squashfs; experiments that need a writable file go in `/data` or `/run`.
- banana-ui's display copy is CPU nearest-neighbour through `PanelFit`; if `BANANA_FRAME_STATS=1` shows the copy
  dominating, move to DPU plane scaling/rotation (atomic SRC/CRTC rects; ROCKNIX's patch 0066 enables inline rotation).
- Emulators and the SDL2 panels patch draw on the panels themselves: they need their own rotation handling (M4).
- Never flash anything to the Thor's UFS partitions other than `abl_a/abl_b`, and only with the ABL backed up.

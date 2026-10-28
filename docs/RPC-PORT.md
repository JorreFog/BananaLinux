# BananaOS for the Retroid Pocket Classic (SM4450 Rev Q)

Branch: `retroid-pocket-classic` (worktree `~/banana-rpc`).

## Strategy

Keep **BananaOS userspace** (Buildroot, banana-ui, bananad, library/settings,
A/B squashfs, exFAT data) and replace the RK3568/U-Boot hardware layer with
**ROCKNIX's SM4450 layer**:

| Layer | Source |
|---|---|
| Kernel | mainline 7.2 + ROCKNIX SM4450 patches (49) |
| Devicetree | ROCKNIX `sm4450-retroid-pocket-classic-revq.dts` (+ common) |
| Bootloader | **OpenABL** already flashed; reads extlinux from FAT |
| Firmware | ROCKNIX extra-firmware: ADSP, WPSS, AW88166 ACF, QCA BT |
| GPU | Mesa freedreno (gallium) + **Turnip** (freedreno Vulkan) |
| Input | kernel `rsinput` (ROCKNIX patch 0016) → evdev |
| Fan | PM6450 PWM + `pwm_fan` (kernel) |

## Boot layout (SM4450, no U-Boot)

```
p1  64M FAT  BANANA-BOOT   /a/Image, /a/<dtb>, /extlinux/extlinux.conf
p2 256M      banana-root-a  squashfs (ro)
p3 256M      banana-root-b  empty until first update
p4  rest     BANANA         exFAT, created on first boot
```

`extlinux.conf` labels `a`/`b` select the root slot via
`root=PARTLABEL=banana-root-a|b`; OpenABL parses it like U-Boot. Updates rewrite
`DEFAULT` and the inactive slot, exactly as BananaOS already does.

## Current tree

```
buildroot/board/retroid-pocket-classic/
├── dts/                     ROCKNIX SM4450 DTS (audio channel fix applied)
├── linux-rocknix.config     ROCKNIX 7.2 arm64 config
├── linux-patches/           49 ROCKNIX SM4450 patches
├── firmware/                ADSP/WPSS/AW88166 + GPU zap + QCA BT
├── linux-banana.fragment
├── extlinux.conf.in
├── genimage.cfg.in
├── post-build.sh
├── post-image.sh
└── README.md
buildroot/configs/banana_retroid_pocket_classic_defconfig
```

## Milestones

### M0 — Build and boot to console/SSH
- Buildroot image builds with the SM4450 kernel/DTB.
- OpenABL boots `Image` from FAT; squashfs root mounts.
- SSH up; no reliance on banana-ui yet.

### M1 — Single-screen banana-ui
- `kms.cpp`: accept **one** connected panel (currently hard-requires two).
- Layout engine: a `single` layout mode; map home/library/settings to one
  1240×1080 screen instead of `top`/`bottom`.
- Theme/UX pass; remove lid/top-panel-only features.

### M2 — bananad on SM4450
- Remove RK817/RK3568 paths; use SM4450 PMIC/charger, power key, fan curve.
- No hall/lid; suspend via `/sys/power/state`, wake on power key.
- Wi-Fi modules: `ath10k_snoc`/WCN3950 instead of RTL8821CS.

### M3 — Audio, input, GPU
- AW88166 left/right (channel fix already in DTS), WCD937x headphones.
- rsinput gamepad mapping (13 keys + L2/R2), no sticks/C/Z.
- Mesa freedreno; **Turnip** (freedreno Vulkan) built too, see the Turnip section below.

### M4 — Updates and flasher
- A/B update script writes slot b + extlinux DEFAULT (already designed).
- Banana flasher/web tools adapted to the SM4450 image.
- First-boot data partition (`firstboot`) unchanged.

## Immediate TODOs

1. Run `make retroid-pocket-classic`; fix defconfig/Kconfig fallout.
2. Add single-screen detection: if only one DRM connector, skip banana-ui and
   start dropbear/getty for M0 testing.
3. Patch `system/rootfs/etc/init.d/rcS`: SM4450 modules (`msm`, `ath10k`,
   `pwm_fan`), remove `panfrost`/`rtw88_8821cs`.
4. Missing firmware: WCN3950 Wi-Fi (BT QCA files copied; Wi-Fi `.mbn` still to
   source), plus any GPU `a630_sqe.fw` path check.
5. Boot test via OpenABL; confirm extlinux path and `Image` EFI-stub loading.

## Risks

- Banana-ui is fundamentally dual-screen; M1 is the largest code change.
- ROCKNIX patches target 7.2; Buildroot tarball must stay at 7.2 (not 7.2.7).
- OpenABL extlinux uses `/a/Image`; verify it handles a raw EFI-stub Image
  (ROCKNIX used an Android bootimg).
- WCN3950 Wi-Fi firmware not yet in the board tree.

## Build status (2026-10-27)

- SM4450 kernel **builds successfully**: `Image` + `sm4450-retroid-pocket-classic-revq.dtb`
  (and the original/6-button DTBs).
- Patch set is 47 ROCKNIX SM4450 patches; drift fixed for 0011, 0018, 0030.
- Dropped non-RPC patches: FT7131M panel, single-ADC joypad/Bato LEDs.
- Gated Banana's RG DS/RK3566 post-patch hooks in `buildroot/external.mk` on the RK3568 DTS
  name, so they no longer leak into SM4450.
- `CONFIG_INITRAMFS_SOURCE` set empty for direct squashfs boot.
- Full rootfs/image build running.

Remaining before first boot: single-screen UI (KMS one connector), RPC-specific
rcS/inittab (SM4450 modules, SSH bring-up), WCN3950 Wi-Fi firmware.

## QEMU test path (2026-10-27)

The image built (`images/banana-retroid-pocket-classic.img`, 593 MB; gz 147 MB;
update `banana-retroid-pocket-classic-0.4.1.update.tar`), GPT:
`boot` FAT32 (extlinux A/B) + `banana-root-a/b` squashfs; the data partition is made
on first boot.

For UI/rootfs iteration without the handheld, `linux-qemu.fragment` adds the arm64
`virt` machine's drivers to the same kernel:

- `SERIAL_AMBA_PL011(_CONSOLE)` – `console=ttyAMA0`
- `DRM_VIRTIO_GPU` + `VIRTIO_INPUT` – the two panels and the keyboard/tablet
- `MMC_SDHCI_PCI` – the card as `mmcblk0`
- `RTC_DRV_PL031`

Already in the ROCKNIX base: PCI, PCI_HOST_GENERIC, PCI_MSI, VIRTIO, VIRTIO_PCI,
VIRTIO_NET, VIRTIO_CONSOLE, ARM_AMBA.

Run (no root needed in interactive mode):

    tools/qemu-boot.sh output/retroid-pocket-classic/images/banana-retroid-pocket-classic.img.gz --interactive

The harness overrides APPEND with `console=ttyAMA0 video=Virtual-1:640x480e
video=Virtual-2:640x480e banana.top=Virtual-2 banana.touch=virtio`, so the stock
two-panel UI comes up as-is. The RPC's one 1240x1080 panel is a later UI change
(`Display::openSingle()` + single-screen theme), testable with `max_outputs=1`.

Not testable in QEMU: SM4450 DSI panel/timings, backlight, Adreno, fan/PMIC,
rsinput gamepad, touch controller, Wi-Fi/BT.

## Wi-Fi gap (found while packaging)

`wifi@22800000` is `qcom,wcn3990-wifi` (ath10k_snoc), and the WLAN firmware comes
from the `wpss` remoteproc (`qcom/sm4450/RPClassic/wpss.mbn`, present). Still needed:
`ath10k/WCN3990/hw1.0/firmware-5.bin` (+ board data) in `/lib/firmware`, and
`modprobe ath10k_snoc` in `rcS` instead of `rtw88_8821cs`.

## QEMU smoke test: passed (2026-10-27)

`tools/qemu-headless.sh` booted `banana-retroid-pocket-classic.img.gz` (md5
`e0dcc20729c32d5a2d6aa4f917c5f7b8`) on the arm64 `virt` machine:

    rcS: started                   1.89 s
    rcS: slot a booted             2.11 s
    rcS: data partition ready      4.19 s
    banana-ui: panels open         5.62 s
    banana-ui: the menu            8.47 s

First boot created and formatted `banana-data` (exFAT, p4), renamed it, mounted it as
`/data`, and the UI drew the first-time setup on both virtual panels. No errors in the
serial log except the harmless "partition table busy" from the mounted card.

The card is read exactly as OpenABL reads it (extlinux DEFAULT/LINUX/root=), so this
also validates the boot partition layout, the squashfs root and the init sequence.

## Single-screen UI (2026-10-27)

The RPC has one 1240x1080 panel; Banana's UI was built for the RG DS's two 640x480 panels.

- `Display::openSingle()` (src/kms.cpp): one connected connector (the named one first, else any with a mode),
  `panels()` says how many `present()`/`restore()` drive; `lease()` is refused on a one-panel display, so a game
  takes the whole screen instead of being handed a panel.
- `main.cpp` tries the two-panel open, then the single one; the scale for one panel is `width / 640` (1.9375 on
  1240x1080), so the layout canvas is 640 wide and as tall as the panel allows (640x557).
- `themes/pocket.json`: the single-screen theme (status bar, shelf of systems at home, game list in the library,
  a launch card). Selected automatically when there is one panel and the chosen theme is one of the two-screen ones.
- `Settings` in single mode: the page's list is drawn on the only screen and what the second screen carried (the
  setting's description and help) goes in the strip under it (`Settings::footer`).

## Fixes found by testing the image in QEMU

- **Boot partition too small for A/B**: 64 MB held one `Image` (38 MB) and no second slot; the update failed with
  `cp: write error: No space left on device` writing `/boot/b/Image`. Now 160 MB (two kernels + DTBs + slack).
- **A getty that couldn't start**: the inittab line must not contain `:` (busybox init splits the line on it), and on
  QEMU `ttyS2` exists (an unused 8250 port) so a plain "first UART that exists" picked a dead port. `console-shell`
  now follows the kernel's `console=` first, then the boards' UARTs, and waits (no respawn loop) when there is none.
- **The framebuffer console over the menu**: with VT/fbdev/logo on, the kernel repainted the aesthetic boot logo over
  the panel as soon as banana-ui released the DRM master for a game (and stayed there if the game failed). The RPC
  fragment now disables `VT`, `DRM_FBDEV_EMULATION`, `DRM_MSM_KMS_FBDEV`, `FRAMEBUFFER_CONSOLE` and `LOGO`, as
  Banana's own handhelds do.
- **Reopening the display after a game** only tried the two-panel open; it now falls back to the single one too.

## QEMU end-to-end test

    tools/qemu-e2e.sh output/retroid-pocket-classic/images/banana-retroid-pocket-classic.img.gz

boots the image (one panel by default), skips the first-time setup, fetches a ROM and the update package from a
server it runs, applies the update, and boots slot b -- failing on any step. No root needed: files go in with the
guest's `wget` and commands over the serial console (`tools/qemu-console.py`), screenshots with the QEMU monitor
(`tools/qemu-monitor.py`).

Last run: first boot -> data partition -> menu in 8.5 s; game visible in the library; update written to slot b and
made the default; next boot `rcS: slot b booted` with the game still on the card.

## Panel rotation (found on the hardware, 2026-10-27)

The first card written showed the menu upside down and mirrored. Two bugs in the rotation added with the
single-screen work:

1. `of_drm_get_panel_orientation()` maps the device tree's `rotation = <90>` (which is counter clockwise) to
   `DRM_MODE_PANEL_ORIENTATION_RIGHT_UP`, not `LEFT_UP`; the connector's value was read the other way round, so the
   RPC took the 270 path.
2. The 270 path wrote `col = y, row = x`, which is a transpose with the wrong handedness -- a mirror, so the text
   read backwards. A quarter turn the other way is `col = y, row = W-1-x`.

Both fixed: every path in `Display::turn()` is a proper rotation (checked by turning a synthetic screen each way and
rotating the result back). The device's connector now gives 90 (the panel is turned 90 CCW in the handheld, so the
image turns 90 CW) and the menu is upright.

`banana.rotate=0|90|180|270` on the kernel command line still overrides it, for a panel whose device tree or
orientation property says something else: edit both `APPEND` lines in `extlinux/extlinux.conf` on the card's FAT
partition and reboot.

Touch is still mapped as if the panel's axes were the screen's: the FT5452 reports 1080x1240 (the panel's own axes),
so the touch coordinates need the same rotation once the direction is confirmed.

## Audio: the bind chain, and what is still missing (2026-10-27)

Getting the card to bind took four separate things, found one at a time on the device:

1. The DT card is `"qcom,sm4450-sndcard", "qcom,sm8450-sndcard"` -- the **sc8280xp** machine driver (audioreach),
   not the sm8250/q6afe one. `rcS` loads `snd-soc-sc8280xp` + `snd-q6apm`/`q6apm-dai`/`q6apm-lpass-dais` +
   `q6prm`/`q6prm-clocks`.
2. The SoundWire controllers need their `iface` clock from the **LPASS RX macro** (`&lpass_rxmacro`), so
   `snd-soc-lpass-{macro-common,rx-macro,tx-macro,va-macro}` must be loaded, and `soundwire-qcom` for the bus; the
   WCD937x then enumerates as two SDW slaves (`sdw:2:0:0217:010a:00:4` RX, `...3` TX).
3. The machine driver could not find `WCD Playback` until those were up.
4. **The audioreach topology**, built from ROCKNIX's own topology sources: `projects/ROCKNIX/devices/SM4450/`
   `topology/RetroidPocketClassic.m4` (copied to `board/retroid-pocket-classic/topology-src/`), expanded with the
   `linux-msm/audioreach-topology` m4 library and compiled with `alsatplg`:
   `m4 -I <audioreach-topology> RetroidPocketClassic.m4 | alsatplg -c - -o RetroidPocketClassic-tplg.bin`
   (plus ROCKNIX's `subgraph-device-i2s-playback-mfc.m4` under `audioreach/`). The result is
   `firmware/qcom/sm8450/RetroidPocketClassic-tplg.bin`, which is the name q6apm asks for.
   Earlier note, kept for the detail: `q6apm` asks for `qcom/sm8450/<model>-tplg.bin` at component probe
   (`RetroidPocketClassic-tplg.bin` here) and fails the card with `-ENOENT` without it. linux-firmware ships no
   Qualcomm topology, and the ROCKNIX SM4450 image has none either (their audio on this device looks unfinished too).
   Building one means the audioreach-topology sources for an sm8450-class graph plus this board's routing
   (WCD937x + two AW88266 on I2S), compiled with `alsatplg`; the stock dump's `vendor_audio/audio/sku_*/mixer_paths_*.xml`
   describes the vendor routing and is the best reference.

The startup race also matters: loaded before the ADSP answers, `q6apm` logs `CMD timeout for [10001021]`
(`APM_CMD_GET_SPF_STATE`) and never retries. Loading the stack after the ADSP is up avoids it.

## The GPU and Mesa (2026-10-27)

Two layers had to agree that the GPU is an **a613**:

- the kernel: `a6xx_catalog.c` needed `.revn = 613` in the a613 entry -- `MSM_PARAM_GPU_ID` returns exactly that
  field, and with it missing Mesa read gpu id 0 and refused the GPU (`fd_pipe_new2: unsupported GPU id 0x0`). With
  it, `debugfs/dri/1/gpu` says `revision: 613 (06010300)` and `gpu-initialized: 1`.
- userland: Mesa 26.0.1's `freedreno_devices.py` has no a613, so `fd_dev_is_supported()` still failed
  (`unsupported GPU id 0x265 / chip id 0x6010300`). Mesa 26.2.0 has `GPUId(613)` in the a6xx gen1 low block;
  `patches/mesa3d/0001-freedreno-add-a613.patch` (applied through `BR2_GLOBAL_PATCH_DIR`) adds the same line.

Also RetroArch's `video_monitor_index` was 2 (Banana's "top panel" on the DS pair); a single connector is only ever
RetroArch's monitor 1, so even with the GPU up it found no display. The template now says 1.

## The menu's cues were being swallowed (2026-10-27)

Reported as "the games have sound, the menu doesn't" and then, once the route was fixed, as **"a normal press is
silent, but if I press fast it makes a sound"** -- which is not a routing symptom at all, it is a timing one, and it
named the bug:

banana-ui closed its ALSA device 150 ms after each cue on this card (bananad's `audio-exclusive` marker made it, to
free the frontend for a game). The RPC's DSP has to instantiate the frontend's graph at the open, and that takes
longer than the cue is: so every *isolated* press spent its whole sound building the graph and never played, while a
fast run of presses left the device open between them and was heard.

Fix: the menu holds its stream open between cues exactly as it does on the RG DS (20 s quiet, then close). The game's
claim on the frontend does not need the early close -- `Sound::release()` already gives it up, and `runGame()` calls
it for every launch -- and `run-retroarch` now waits for the frontends to report themselves free before RetroArch
opens one. bananad's `audio-exclusive` marker is gone with it: nothing reads it.

## Booting the fixed image, and what the boot log said (2026-10-27)

The update went over SSH (`/usr/libexec/banana/update apply`), the handheld came up on slot a and read its own boot
log back:

    rcS: sound routed (0 tries)    9.22 s
    bananad: bottom panel's backlight: ae94000.dsi.0        <- the one-panel fallback, working
    bananad: sound: the route needed 15.0 s more than rcS allowed   <- my own new wait, burning boot
    bananad: running: lid sleeps, auto-sleep 10 min, deep            <- the no-lid rule did NOT fire

Three bugs, all in what I had just written:

- **`has_lid()` asked the wrong question.** It used `EVIOCGSW`, which is the *state* of the switches (and is what
  `read_lid()` needs: "is the lid closed right now"); a slate's state is all zeros, so it answered "no lid" only by
  accident. The question "does this machine *have* a lid switch" is the **list** the driver advertises:
  `ioctl(fd, EVIOCGBIT(EV_SW, ...))` and bit `SW_LID`. The Retroid Pocket Classic's switch devices are the power key
  (EV=3, no switches at all) and the codec's jack (switches `0xd4`: headphone, microphone, line-out, charging --
  never SW_LID), so the corrected check says slate, and the rule now disables the idle suspend.
- **the image's own default counted as a wish.** `load_conf()` read `/etc/banana/bananad.conf` (Banana's defaults,
  which say `autosleep_minutes = 10`) and `/data/banana/bananad.conf` (a person's) the same way, so "written down" was
  always true and the slate rule stood aside. `load_conf(path, asked)` now marks only the data-partition file as an
  ask, and the setting re-read on SIGHUP clears the flag first -- so the idle behaviour a slate shows is: dim after
  two minutes, never suspend, power button still sleeps, and `autosleep_minutes` in the settings asks for the timer
  back.
- **the route wait waited for the wrong thing.** The 15 s it burned was real waiting for a control that is *meant* to
  read `off`: `headphones` was 1 at boot, so `audio_route_ar()` was writing `off,on` to the speaker mixer each time
  round and reading it back as `off`. It now waits only when the jack says nothing is plugged in, gives up after 3 s,
  and logs what the control actually read (`'%s'`) instead of a duration -- because the interesting question is not how
  long but *what the DSP answered*.

The boot after those three changes reads, in order:

    sleep: no lid switch, so the idle timer dims (2 min) but never suspends; autosleep_minutes asks for it
    sound: the jack says nothing in at start
    (no "still reads" line at all: the route was there)

What is **not** settled: why the route read `off,off` for those 15 s on the earlier boot. `off,off` with the jack
saying "nothing in" means either the boot-time jack read was wrong on that boot (the WCD937x's MBHC is known to answer
before it has measured anything, and it is the one thing here that would explain it: bananad would then have been
faithfully writing `off` to the speakers), or the mixer simply wasn't ready and every write was dropped. The new
`sound: the jack says ... at start` line distinguishes the two on the next boot that shows it, which is why it logs
the jack's answer and not just a duration. `evtest --query <jack> EV_SW SW_HEADPHONE_INSERT` at idle (no evtest in
the image; `headphones_in()`'s own EVIOCGSW read, or a two-line test program) is the direct check, still to do.

## The menu went silent, and why (2026-10-27)

The games had sound and the menu did not -- after the first game of a session, from then on, until the reboot. That
is the DSP's own rule turned around: **the frontend's graph takes the mixer route when it is instantiated, and it is
torn down when its last stream closes.** So the route has to be in place before *every* open, not before the first
one:

- `run-retroarch` always re-asserted the route before starting RetroArch -- which is why games kept working.
- banana-ui's `Sound` did the prime-and-route dance **once per process** (`dspRouted`). Its first cue of the session
  was silent if the route wasn't in place yet (the boot race: `rcS` writes it at 9.2 s and the menu opens its stream
  around then), and after a game closed the graph every later cue opened an *unrouted* one -- silent for the rest of
  that stream's life.

Fix: banana-ui now runs the dance at **every** device open (with the RPC's `audio-exclusive` it closes the device
between cues anyway, so it is a few milliseconds on a cue that is already starting), and it applies the route
*bananad chose* rather than hardcoding the speakers: bananad publishes `/run/banana/audio-route` (`control=values`
lines, from the jack: speakers or headphones) and banana-ui and `run-retroarch` both write those, falling back to
"speakers on" when the file isn't there yet (a card from an older build, or a cue before bananad has run).

Boot-time evidence, kept because it is the whole story: the kernel's one complaint

    MultiMedia1 Playback: ASoC: no backend DAIs enabled for MultiMedia1 Playback, possibly missing ALSA
    mixer-based routing or UCM profile

appears exactly once, at the menu's first open, and never again once the route is in place before it. Verified with
RetroArch on the device: `pcm0p` `RUNNING` with the hardware pointer advancing and no new complaint -- and the user
hearing the game.

## The handover between the menu and a game (2026-10-27)

The two audio reports -- "the menu's cues are silent" and "no sound in PlayStation" -- turned out to be the same bug
one layer further down, and it is a race with the DSP rather than with the mixer.

An ALSA substream reports `closed` as soon as the **frontend** is released; the audioreach **graph** behind it is
torn down by the ADSP some moment later. A frontend opened inside that window comes up half connected: the PCM runs
-- `pcm0p` `RUNNING`, the hardware pointer advancing at exactly the sample rate, the DSP eating the data, the route
mixer reading `on` -- and the speaker amplifiers never start. Counted on the device with `dmesg | grep -c 'start
failure'`, each `aw88166 0-00xx: start failure (-1)` being one amp that was asked to start into a half-built graph:

| the frontend was opened | new amp failures |
|---|---|
| ~0.4 s after the menu's stream was released (a game launching) | **2**, both amps, every time |
| 20 s after anything had let go of it (the same game, same config) | **0** |

So `run-retroarch` waiting for the substreams to report themselves free was necessary and not sufficient: free is
the frontend, not the graph. Both sides of the handover now wait one beat more, **and only when something had to be
waited for**, so nothing is paid when the frontend was already idle: `run-retroarch` before it writes the route and
execs the core, and banana-ui's `Sound` before it reopens after a game (the `wasHeld` flag).

The right long-term fix is in the kernel: `q6apm`'s DAI `.close` returns before the graph destroy has been
acknowledged, so any two programs handing a frontend over have to guess at the gap (`sound/qcom/q6apm-dai.c`, waiting
for the graph-close completion). When that lands, both userspace waits should come out again; until then the amp
count above is the way to tell whether the wait is still earning its keep.

Related, found in the same hunt: `run-ps1`'s panel-size lookup globbed `card0-*`, which is the RG DS's numbering --
the RPC's panel is on `card1` (msm), so PlayStation scaling was being chosen by the *fallback* rather than by the
panel. It now reads whichever connector has a mode, and lands on integer scaling for the right reason (960x720 at 3x
inside the 1080-tall landscape view).

## No DS or 3DS on this handheld (decided 2026-10-27)

The Nintendo DS and 3DS are out of the Retroid Pocket Classic build, and a RetroArch core (melonDS or DSuaN) can come
later as its own decision. Not because the SoC is too weak -- it is far stronger than the RG DS Plus -- but because:

- **the screens.** The DS's whole layout is two screens, and `run-drastic`/`run-bananads` put one on each panel, which
  is also how their frame pacing, sound lock and touch were written. One 1240x1080 panel would want a different
  composition (both screens side by side, each at 620 px wide, half the DS's own pixel size).
- **the launchers are performance work for that one board.** Their bulk is RG DS Plus tuning: the panel-rate audio
  lock, the governor that hunts for the lowest clock a game still runs full speed at, `banana-panel-tune`'s pixel
  clock, two swap-chain images with the fast-forward patch. None of that applies here, and none of it was written to
  be ported.

What the branch does instead:

- `run-nds`, `run-drastic`, `run-bananads`, `run-3ds`, `nds-game-shader`, `nds-saves` (and the PICO-8/homebrew-bank
  extras) are gone from `system/rootfs`, and `[nds]`/`[3ds]`/`[pico-8]`/`[dodakvarter]`/`[rocknixdsbank]` from
  `system/rootfs/etc/banana/systems.conf`. The RPC's shelves are the RetroArch ones: gb, gbc, gba, nes, snes,
  megadrive, psx, n64, ports.
- **banana-ui keeps the DS code** (it is the same UI as the RG DS, and deleting it would be a fork for a fork's
  sake), but it no longer *offers* it: `Library::hasSystem("nds")` feeds `Settings::setHasDs()`, and the Nintendo DS
  settings group is built only when the shelf is there. The library's X-on-a-game page already keyed on the game's
  system, so it goes the same way. Without that gate the handheld shows a page of settings that write files no
  installed program ever reads.
- **the test harness keeps its own shelf list**: `ui/tests/systems.conf` (the DS-era file, kept whole), which
  `ui/tests/run.sh` now passes with `--systems`. The harness tests banana-ui's views -- the DS banner art, the
  per-game page, the settings group order -- and those paths exist on the RG DS whether or not this image ships them;
  pointing it at the RPC's trimmed list tested a UI with half its screens missing and failed on 5 checks. `make test`
  is green again.

## Turnip: SwanStation's Vulkan renderer (2026-10-28)

The a613 is an `a6xx gen1 low` part: it has no hardware multiview, so Turnip reports Vulkan **1.0** (Mesa's
`tu_device.cc`: `tu_has_multiview ? 1.3 : 1.0`). That is enough -- RetroArch creates its instance at `1.0`, and
SwanStation's core asks for `VK_API_VERSION_1_0` too.

How it is built and wired:

- **Mesa**: Buildroot 2026.02.3 predates upstream's `BR2_PACKAGE_MESA3D_VULKAN_DRIVER_FREEDRENO` (added 2026-03), so
  `buildroot/buildroot-patches/0001-mesa3d-add-freedreno-vulkan-driver.patch` adds it and the Makefile applies that
  to the downloaded Buildroot (idempotent: a patch already in place is left alone). `freedreno` joins
  `-Dvulkan-drivers`.
- **Loader**: `vulkan-loader` and `vulkan-tools` (for `vulkaninfo`) in the defconfig.
- **RetroArch**: built `--enable-vulkan --enable-vulkan_display` (it was `--disable-vulkan`, and it even logged that
  it was not compiled against Vulkan).
- **`run-retroarch`**: sets `video_driver = vulkan` + `video_context_driver = vk_wayland` when SwanStation's
  renderer is Vulkan, `gl` + `wayland` otherwise -- a libretro core's hardware context comes from the video driver --
  and starts RetroArch under cage, the kiosk compositor that turns the output (see the section below).
- **`run-ps1`**: writes `swanstation_GPU_Renderer = "Vulkan"` and `ResolutionScale = "5"`.

On the device `vulkaninfo` reports `Turnip Adreno (TM) 613`, `DRIVER_ID_MESA_TURNIP`, Mesa 26.0.1, apiVersion
1.0.335. Crash Bandicoot runs a steady 60 fps at 5x for ~11-13% of one core, where the software renderer and the
freedreno OpenGL path both fell short.

Two things it does not fix: the first seconds of a game still crackle while the amplifiers warm up (the same rise
the boot log shows), and RetroArch's own menu stayed sideways until the games moved under cage (see below). One trap
to avoid: the menu's "Safe (128 ms)" audio latency produced *no sound at all* -- the DSP will not take a 128 ms
buffer and RetroArch gives up on the audio driver entirely -- so that option is gone and `run-retroarch` clamps
anything unexpected to 64.

The menu is a software texture the drivers blit with `mvp_menu`; the notifications, the FPS counter and the
widgets are drawn through `gfx_display`, which asks the driver's `get_default_mvp`. Patches 0005/0008 answered that
for gl3/gl2 and 0010 for the menu here; 0011 answers `mvp_menu` for the Vulkan driver too, or those stay in the
framebuffer's portrait orientation while the menu is upright. ROCKNIX needs none of it -- RetroArch runs windowed
under sway, which turns the output. Under cage (below) they no longer matter -- with `video_rotation` at 0 they are
inert, and they are kept for a card that runs RetroArch on KMS itself.

Two audio traps turned up on the way, both of which read as "the sound broke":

- **The volume keys.** They are bananad's and change the card's mixer, which the DSP latches when a frontend's graph
  is instantiated -- so a press during a game was inaudible until the menu's next open, and all the way down it was a
  silent handheld with nothing on screen to say why. In a game bananad now writes `VOLUME_UP`/`VOLUME_DOWN` to
  `/run/banana/retroarch.cmd` and RetroArch changes its own volume live, with its OSD; only the menu edits the mixer.
- **The menu's open racing the route.** The DSP refuses a mixer write until it is ready, so rcS's and bananad's own
  retries ("still reads 'off,off' 3 s after rcS gave up") can be over before it takes, and the menu then opens and
  latches an unrouted graph that is silent for the whole session. banana-ui now reads the route back and retries the
  write before it opens.

## Solved: the whole picture upright -- the games run under cage (2026-10-28)

`video_rotation = "3"` turns the **content** only: RetroArch applies it in the video driver's rotation
(`gl2_set_rotation`, `gl->rotation`, used for `gl->mvp` and the content blit), which is why the game is upright and
the rest of RetroArch is not. RetroArch's own menu is a different path (`gfx_display`'s `get_default_mvp`, which
answers with `mvp_no_rot`), and so are the settings rows, the notifications, the FPS counter and the volume widget --
three drivers, four patches, and the game and the interface never turn together. ROCKNIX never sees any of it because
sway turns the output and RetroArch runs windowed under Wayland.

So Banana does the same: a game now runs under **cage**, the kiosk compositor (one application, fullscreen, MIT, in
the image). cage takes DRM master on the panel, and wlroots' DRM backend reads the connector's "panel orientation" --
the same property banana-ui reads for its own `Display::turn()`. Everything above it is composed once, in the shape
the user is looking at, and turned by the compositor on the way to the panel: the game, RetroArch's menu, the OSD and
the widgets. `video_rotation` is 0 (`run-retroarch`), and the per-driver rotation patches are left for the no-cage
fallback only.

Three things this needed, each of which first looked like something else:

| symptom | cause | fix |
|---|---|---|
| RetroArch exits at once under cage (`Failed to create KHR_display surface`) | Buildroot does not rebuild a package when only its `.mk` changes, so `--enable-wayland` had never reached the build: `HAVE_WAYLAND = 0`, and RetroArch's Vulkan driver fell through to `khr_display`, which cage blocks | force the reconfigure, and use the Wayland *context* drivers (there is no `wayland` video driver: `vk_wayland` for Vulkan, `wayland` for GL) |
| `[Vulkan] Instance does not support required extensions` | the same trap twice more: `vulkan-loader` had `BUILD_WSI_WAYLAND_SUPPORT=OFF` and Mesa was built without the `wayland` platform, both from before `BR2_PACKAGE_WAYLAND` existed | rebuild both; `VK_KHR_wayland_surface` then reaches the app |
| the panel was still sideways under cage | wlroots reads the panel orientation but leaves the choice to the compositor; sway calls `wlr_drm_connector_get_panel_orientation()`, cage never did | `buildroot/patches/cage/0001-...`, applied where cage builds the new output's state |

**A suspend that could never come back.** With cage running, a short power press put the handheld to sleep and it
never woke. bananad was stuck in `vt_move_to_console`, the kernel's own VT switch on suspend, waiting for a VT that
can never be released: libseat's embedded seatd makes the seat **VT-bound**, which opens a VT, sets it to
`VT_PROCESS` and activates it -- and bananad has the game's whole process group (cage included) `SIGSTOP`ped by then,
so nothing can answer the kernel's `kill_pid(..., relsig)` and the switch waits forever. The panel's backlight is
off by then, so it reads as "asleep and dead to the power key" rather than "wedged". `SEATD_VTBOUND=0` in
`run-retroarch` makes it a plain seat: the devices are opened, nothing is done to a VT this kernel does not have.
Verified on the unit: a short press sleeps and a second one wakes it, the game and the menu back 49 ms after the
kernel returns.

**Nothing on the RPC can wake it on a timer.** Its PMIC RTC has no alarm (`qcom,no-alarm`: the ADSP owns the alarm
interrupt), so there is no `wakealarm` attribute to write and `sleep_check_hours` -- bananad's four-hourly battery
check, and the safety net that brings a sleeping handheld back by itself -- can never fire here. Waking is the power
key or the charger, and nothing else. A handheld left asleep with no one to press it drains flat.

## RetroArch's black panel (2026-10-27)

A game started from the menu went to a black screen seconds in and would not answer: the picture path was the first
thing anyone suspected, so it was tested one layer at a time with `tools/rpc-gpu-probe.c` (built with
`tools/rpc-probe-build.sh`, run from `/data`; `tools/rpc-hang-watch.sh` keeps the kernel's buffer on the card, synced
once a second, so a hang that ends in the power button still leaves its last second behind):

| probe | what it touches | result |
|---|---|---|
| `offscreen` | renderD128, GBM, EGL, 120 draws with `glFinish` | **ok** — `Mesa/26.0.1 freedreno FD613 OpenGL ES 3.2`, no hang |
| `dumb` | a real modeset on the panel + 64 flips, no GPU (what banana-ui does) | **ok** |
| `kms` | the GPU's own GBM buffers scanned out (what RetroArch does) | **ok**, 120 frames |

So the a613, the DPU and the splash handover are fine together. The cause was one line in the card's own
`/data/retroarch/retroarch.cfg`: `video_driver = "null"`. RetroArch then runs the core and its sound with **no video
driver at all** — the menu has already handed the panel over, so what is left is a black panel with a game running
behind it and nothing on it to press. It looks exactly like a hang.

It gets there because RetroArch **writes that file back when it quits**: one trip through its own menus (Video >
Output > Video Driver), or a card written by an older build or by debugging, and the value sticks for every later
game. The image's template has always said `gl`.

Fix: `run-retroarch` now forces the keys a picture and sound depend on at **every** start, not once
(`video_driver`, `video_context_driver`, `video_fullscreen`, `input_driver`, `audio_driver`, `audio_device`, and
`midi_driver = null` — there is no `/dev/snd/seq` on this image and RetroArch logged two errors and a failed init for
it on every launch). Settings Banana does show (the screen, the shader, the latency, VSync) are still the card's.
Anything RetroArch is free to keep stays.

Found on the same pass, all fixed:

- **`banana-perf` tuned `policy0` only.** The RG DS is one cluster of four A55, so that was the whole SoC there; the
  RPC has `policy0` (six Cortex-A55) *and* `policy6` (two Cortex-A78). The menu's "powersave" capped the little
  cores and let the big ones run free, and a game's profile never raised the cluster most likely to draw the frame.
  Same for banana-ui's `ClockBoost`, which raised `policy0`'s minimum while the A78s were the ones scrolling the
  list. Both now go over every `policy*`.
- **the wait for the DSP before RetroArch opens sound** was `sleep 0.25`, and the log still shows the abort it was
  meant to cover (`Aborted`, exit 134: the ALSA driver's `snd_pcm_avail` when the menu's graph is still being torn
  down). It now reads `/proc/asound/card0/pcm*p/sub0/status` and waits until no frontend is
  `RUNNING|SETUP|PREPARING|PREPARED|DRAINING|PAUSED` (up to 1.2 s), then takes the device anyway; the 134 retry stays.
- **rcS's route wait gives up after 6 s** and the handheld is then silent for the session with nothing in the log to
  say why. bananad now does the same thing again at its own start, with the route **read back** through amixer (the
  DSP's mixers print `: values=on,on`, not the `value:'on'` a Rockchip codec does), up to 15 s, and logs how long it
  took. It also sets the DSP's `stream0.vol_ctrl0 MultiMedia1 Playback Volume` and the amps' volumes there, so the
  same retry restores the level after a slow card.
- **a game's RetroArch log** only existed in the leased-panel mode (on the card's RAM, `/run`). The single-panel mode
  RetroArch actually runs in on this handheld now writes `/data/banana/logs/retroarch.log` on the card.
- `rcS` said "a game takes MultiMedia2": it does not — both frontends are routed to the same speakers and RetroArch
  opens MultiMedia1 like the menu does (that is why the route is re-asserted before it starts). The comment now says
  what happens.

One thing to know when testing over SSH: the handheld **suspends after 10 idle minutes** (bananad's default
`autosleep_minutes`), which looks like a dead network from the outside — `printf 'autosleep_minutes = 0\ndim_minutes =
0\n' >> /data/banana/settings.conf` for a session (bananad re-reads it within 5 s), and take those lines off again
afterwards.

## Sound on the RPC: what it took (2026-10-27)

Beyond the module chain above, three things about *this* DSP's behaviour decide whether audio is heard:

1. **The route must be in place before a frontend is opened.** `SENARY_MI2S_RX Audio Mixer MultiMedia1` (the
   switch that mixes the first frontend into the speaker I2S) is read by the DSP when the stream is instantiated:
   setting it while a stream is open has no effect, and the stream stays silent for its life. `rcS` therefore sets
   it -- and reads it back, retrying -- *before* `banana-perf ui` starts, because the menu opens its playback stream
   at startup and keeps it.
2. **Only `MultiMedia1` produces sound.** `MultiMedia2` accepts writes ("played 96000 frames") and is silent, so
   everything shares the first frontend: the menu releases it when a game starts (`Sound::release()`), and
   `run-retroarch` re-asserts the route right before RetroArch opens it.
3. **There is no UCM profile and no ALSA plugin directory** in the image (`/usr/lib/alsa-lib` is empty), so the
   `hooks` PCM plugin can't be used for routing; `plug` and `asym` are built into libasound and are fine.

The menu's own stream volume (`stream0.vol_ctrl0 MultiMedia1 Playback Volume`) and the amps' `Left PCM`/`Right PCM`
(-12.75 dB at 460/511) are set by `rcS` too; `bananad` reports on failure to syslog.

Also learned the hard way: a stray `retroarch` left by an SSH test held `/dev/snd/pcmC0D0p` and made everything
else "device busy" -- one of the "no sound" reports was that, not the platform.

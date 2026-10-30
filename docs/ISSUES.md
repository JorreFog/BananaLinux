# GitHub issues: cause and the fix

Snapshot of every issue in [JorreFog/BananaLinux](https://github.com/JorreFog/BananaLinux/issues?q=is%3Aissue) as of 30 October 2026 (Europe/Stockholm): **98 issues, 76 open, 22 closed**. Many open tickets already shipped in 0.4.2 or 0.5.0; GitHub was not always closed. Causes below are from the tree on `main` (`12474fb`) plus the issue threads.

A live copy of the same notes is [BananaOS issue analysis](https://cursor.com/canvas/store-cfd2f98a-3e84-4197-a0d9-3cf70b8f59ee/f6c2e279-b7ee-40e3-9656-d2466dba4c30) (clusters, charts, the 25 remaining code changes, open PRs, and tickets to close).

## How to read this

- **Shipped** means the requested behaviour is in a release. Close those tickets after a quick check.
- **PR** means an open pull request already contains the fix.
- **Needed** means the file and the change are identified here and are not in `main`.
- **Hardware / emulator / product** means BananaOS can document, wrap, or work around, but the real limit is the SoC, DraStic, melonDS, or a new app.

## Clusters

These tickets are the same bug under several numbers. Fix the cluster once.

### A. Charge while off, gauge, and the amber LED (`#8` `#34` `#99` `#104` `#116`)

The RG DS Plus *does* take current from the charger with the kernel down. What fails is everything that needs the RK817 awake:

- The amber LED is a PWM on the RK817. Fully off, that rail is down, so there is no light (`#116`, part of `#34`/`#99`).
- Until 0.5.0 the fuel gauge could ignore an off-charge and keep the old percent (one unit showed 25% after 22 hours on the charger with a full battery). 0.5.0 starts from a voltage floor so the percent jumps to reality on boot (`#34`, `#99`, `#104`).
- Sleep *on* the charger is a different path: since 0.4.2 bananad stays dark and awake so the PWM LED can stay on (`#8`).

**Fix still to land:** [PR #123](https://github.com/JorreFog/BananaLinux/pull/123) (`bananad --charge`) keeps a tiny on-charger context so the LED can light without a full boot. Confirm it does not undo a real power-off. Keep `#34` as the tracker until a long off-charge on 0.5.0+ shows the right percent *and* a light.

### B. Sleep drain (`#79`, with `#64` `#46` `#85`)

`CONFIG_USB_GADGET` is unset in `buildroot/board/rgds/linux-banana.fragment`. The DWC3 port then stays host-only, xHCI halt returns `-110`, and older bananad stuck to s2idle after two failed suspends (~25% overnight vs ROCKNIX). 0.5.0 retries deep sleep every time and adds **Power off after sleeping**. A 0.5.1 overnight test was still ~11%.

**Fix:** [PR #101](https://github.com/JorreFog/BananaLinux/pull/101) — dual-role OTG so the host can halt, and s2idle fallback for *that sleep only*. Even a working suspend is about 0.5%/h of hardware drain; `#85`'s timer is the safety net. `#64`'s white SuperDrastic screens after ~36 min are a DraStic frame-timer bug, ignored since 0.5.0. `#46` is shutdown re-entering sleep (see cluster D).

### C. Per-game 3D resolution (`#117`, leftover of `#27`)

The game page writes `/data/banana/nds-per-game-res.conf`. `run-bananads` exports `BANANADS_RENDER_SCALE`. `run-drastic` never reads that file and never patches `hires_3d` in `drastic.cfg` (it only swaps `threaded_3d` and the mic). SuperDrastic also has no 3x/4x, only `hires_3d` 0/1.

**Fix:** in `run-drastic`, read the per-game res and swap `hires_3d` for that run the same way `threaded_3d` is swapped, then put Settings' value back.

### D. Shutdown that becomes sleep (`#46`, leftover of `#85`)

A unit that was shut down is found asleep hours later; unplug-then-shutdown can leave black lit screens. `rcK` can suspend again while powering off. 0.5.0 added a Power menu (START then Y) and a shutdown animation so sleep and off look different; it did not stop the re-sleep.

**Fix:** the useful part of closed [PR #84](https://github.com/JorreFog/BananaLinux/pull/84) — pause suspend until poweroff finishes. Trace bananad and `rcK` around shutdown with the lid closed and with the charger just pulled.

### E. Menu: resume chip vs DS icons, START on the plate (`#107` `#128`)

- `#107`: `Library::game()` honours `dsCartIcons`; `Library::system()` fills the Last-played chip from box/cart/label only. Apply `romIconCover` to the resume fields too.
- `#128`: `plateBox()` in `ui/rnds/RndsUI.cpp` has a dark band from y 132; `START` is drawn at `ry+124` on that band. Move the word or drop the band.

### F. banana-sync TLS (`#109`)

`probe()` in `ui/src/sync.cpp` tries `:8092`, `:8080`, `:80` and never `https` / `:443`. RomM on 443 sees plain HTTP and 400s.

**Fix:** also try `https://host` and `https://host:443`. Optional Settings toggle to skip TLS verify for a LAN self-signed cert (off by default).

### G. One bad settings line bricks the menu (`#88`)

Unexpected enums crash at every boot; `pad()` with an empty filler can hang. [PR #73](https://github.com/JorreFog/BananaLinux/pull/73) fuzzes the card and defaults bad values. Merge it.

### H. SuperDrastic fast-forward on R3 (`#113`)

`buildroot/board/rgds/drastic.cfg` sets `controls_a[CONTROL_INDEX_FAST_FORWARD] = 1036` (R3). The Plus has no R3. DraStic's own control editor can also lock the user out of that menu.

**Fix:** default FF to R2 (BananaDS already does). Do not send people into DraStic's mapper.

### I. Colour table is one screen (`#74`, maybe `#129`)

VOP2 has one `GAMMA_LUT`. `kms.cpp` programs one CRTC. Two independent curves are not possible. Same table on both screens may be; `#129`'s green patches need a LUT/panel-tune bisect first.

### J. Bluetooth one-bud (`#118`)

bananad plays only bluez-alsa `a2dp`. OnePlus Buds 4 drop a cup on Low Latency / play left-only on Best Quality. Log the PCM channel count and codec; force SBC stereo or skip a one-channel sink. Games use the same ALSA `default`.

### K. First flash / U-Boot (`#58` `#83` `#102`)

U-Boot lives at sector 64 (~32 KiB). Windows error 5 at ~20 MiB, Defender eating the flasher, or a partition-only write leaves stock/GammaOS booting and Banana with no LED. Not a runtime bug. Document a known-good writer; compare the first 64 KiB to a good image.

## Remaining code on `main`

Open PRs to review first: [#101](https://github.com/JorreFog/BananaLinux/pull/101) sleep, [#123](https://github.com/JorreFog/BananaLinux/pull/123) charge LED, [#73](https://github.com/JorreFog/BananaLinux/pull/73) settings fuzz, [#91](https://github.com/JorreFog/BananaLinux/pull/91) update trial boot, [#94](https://github.com/JorreFog/BananaLinux/pull/94) flight recorder.

Small, local fixes with no PR yet:

| Issue | File | Change |
|---|---|---|
| #117 | `run-drastic` | Read `nds-per-game-res.conf`, swap `hires_3d` for that run |
| #107 | `ui/src/library.cpp` | Resume chip uses `dsCartIcons` / `romIconCover` |
| #128 | `ui/rnds/RndsUI.cpp` | START not on the plate's dark band |
| #109 | `ui/src/sync.cpp` | `probe()` tries https/:443 |
| #113 | `drastic.cfg` | FF default R2 |
| #122 | settings footer | Do not draw help into Back |
| #108 | cart draw | Contain + pad, not cover-crop |
| #87 | bananad / banana-ui start | Keys live before the boot animation |
| #46 | rcK / bananad | Do not suspend during poweroff |
| #41 | settings lists | Time zone / themes / box-art off the UI thread |
| #78 | boot / library | Missing PortMaster folder must not refuse boot |
| #35 | BananaDS pause | Charging bolt on that menu too |

Close on GitHub (behaviour already in 0.4.2 or 0.5.0): `#70` `#69` `#40` `#39` `#36` `#30` `#26` `#23` `#18` `#65` `#8` (if sleep-on-charger LED is confirmed).

## Counts

| | |
|---|---|
| Issues | 98 (76 open, 22 closed) |
| Shipped (close or leftover check) | 48 (31 fully, 17 partial) |
| Open PR already | 8 |
| Cause known, not in tree | 25 |
| Emulator core / SoC / product | 14 |
| Install / docs | 3 |

By area (open only):

| Area | Open |
|---|---|
| Menu | 25 |
| BananaDS | 13 |
| Power | 11 |
| SuperDrastic | 11 |
| Network | 7 |
| Boot | 4 |
| Audio | 2 |
| RetroArch | 2 |
| Feature | 1 |

## Every issue

Each entry is: GitHub status, the work column, where it shipped if it did, the cause in this tree, and the fix.

### [131](https://github.com/JorreFog/BananaLinux/issues/131) — Thermal issue when charging

OPEN · Power · Cause known; change not in tree

**Cause.** Charging while the SoC is awake stacks charge heat on CPU/GPU heat. Off-charge is cooler because the RK3566 is powered down. This is mostly physics, not a menu bug.

**Fix.** If a fix is wanted: lower RK817 input/charge current while on and/or hot (sysfs or DTS), and dim clocks on the charger. Measure board temperature first; do not guess a current.

Files: `system/bananad/bananad.c; DTS rk817 charger`.

### [130](https://github.com/JorreFog/BananaLinux/issues/130) — MTP Responder feature request

OPEN · Network · New emulator, store app, or product choice · #101

**Cause.** There is no USB gadget stack in the image, so there is no MTP function. Gadget is also the missing piece behind deep-sleep USB (#79 / PR #101).

**Fix.** After dual-role OTG lands, add a gadget config (f_fs + umtp-responder, or similar) behind a Settings toggle. Do not enable gadget without the sleep fix: host-only xHCI is why suspend fails.

Files: `buildroot/board/rgds/linux-banana.fragment (CONFIG_USB_GADGET is not set); DWC3 OTG`.

### [129](https://github.com/JorreFog/BananaLinux/issues/129) — Green tints on the screen

OPEN · Menu · Cause known; change not in tree

**Cause.** Green patches on the top panel only under BananaOS (not GammaOS Nano / ROCKNIX). Banana programs VOP2 GAMMA_LUT and its own panel tune; a bad LUT, CSC, or DSI init on one CRTC would tint only that screen.

**Fix.** Compare Banana vs ROCKNIX DTB panel timings and whether a non-neutral GAMMA_LUT is applied to the top CRTC. Bisect banana-panel-tune and Settings > screen colours. If LUT identity still tints, look at VOP2 CSC / panel gamma.

Files: `ui/src/kms.cpp (GAMMA_LUT); ui/src/colors.cpp; banana-panel-tune; DSI panel DTS`.

### [128](https://github.com/JorreFog/BananaLinux/issues/128) — START text overlaps weirdly with selector colors

OPEN · Menu · Cause known; change not in tree · in 0.5.0

**Cause.** plateBox() paints a light band at y 0–14 and a dark band from y 132. START is drawn at ry+124, so the letters sit on the dark band and look cut when the ink is dark.

**Fix.** Move START above the dark band, drop or shorten the bottom band, or draw START on a flat strip of accent.base. Keep the plate 124×162.

Files: `ui/rnds/RndsUI.cpp plateBox() / drawFit("START")`.

### [126](https://github.com/JorreFog/BananaLinux/issues/126) — Bshare: send and receive files over Wi-Fi with LocalSend

CLOSED · Network · Shipped, issue still open or already closed · in 0.5.x · #127

**Cause.** Feature request that shipped as the Bshare Store app (LocalSend protocol).

**Fix.** Already merged. No further OS change required for the request as written.

Files: `Store app / PR #127`.

### [125](https://github.com/JorreFog/BananaLinux/issues/125) — Is undervolt supported?

OPEN · Power · New emulator, store app, or product choice

**Cause.** BananaOS does not expose ROCKNIX-style CPU undervolt. The governors are banana-perf plus SuperDrastic's own limiter.

**Fix.** Only if someone measures a stable OPP table on both boards. A voltage-offset knob without per-board characterisation will hard-lock units. Not a missing toggle on an existing table.

Files: `kernel OPP / banana-perf`.

### [122](https://github.com/JorreFog/BananaLinux/issues/122) — Text description inconsistency

OPEN · Menu · Cause known; change not in tree

**Cause.** The emulator-options help line on the bottom screen runs into the Back affordance. The footer has no reserved width for a long sentence.

**Fix.** Either drop that help line (the rows already name themselves) or clip/wrap it with padding so it never crosses the Back slot.

Files: `ui/src/settings.cpp; ui/rnds help/footer layout`.

### [121](https://github.com/JorreFog/BananaLinux/issues/121) — Group games into folders under a system library

OPEN · Menu · Cause known; change not in tree

**Cause.** The library is a flat list per system. Subdirectories on the card are scanned but not kept as groups, so series dumps become one reel.

**Fix.** Keep the parent folder on each Game and add a folder grouping in the system view (or a 'Folders' sort). Same request as part of #49.

Files: `ui/src/library.cpp`.

### [120](https://github.com/JorreFog/BananaLinux/issues/120) — Multi-row grid layout for the home screen

OPEN · Menu · Cause known; change not in tree

**Cause.** The home and library are a DSi-style one-row reel. There is no grid view.

**Fix.** Add a view mode that lays cartridges in rows (also #49). Keep the current reel as default.

Files: `ui/rnds/RndsUI.cpp (single reel)`.

### [119](https://github.com/JorreFog/BananaLinux/issues/119) — Screen brightness issue

CLOSED · Menu · Shipped, issue still open or already closed · in 0.5.x

**Cause.** 5% was still too bright in the dark.

**Fix.** Closed after a dimmer floor. No remaining code work in this ticket.

Files: `bananad backlight; Settings > Display`.

### [118](https://github.com/JorreFog/BananaLinux/issues/118) — Bluetooth audio — right bud doesn't work in games

OPEN · Audio · Cause known; change not in tree

**Cause.** bananad plays to bluez-alsa profile a2dp only. OnePlus Buds 4: Low Latency drops the right bud at game start; Best Quality is left-only even in the menu. Likely a stereo A2DP / one-bud sink or aptX-LL vs SBC mismatch, not a BananaDS bug.

**Fix.** Log bluez-alsa PCM format and which buds register as A2DP sinks. Force SBC stereo, reject one-channel devices, or fall back when the sink is not 2ch. Games inherit the same ALSA 'default' route.

Files: `system/bananad/bananad.c (bluez-alsa A2DP PCM); system/rootfs bluetooth`.

### [117](https://github.com/JorreFog/BananaLinux/issues/117) — 3D Resolution for individual games doesn't work

OPEN · SuperDrastic · Cause known; change not in tree · in 0.5.0

**Cause.** The game page writes nds-per-game-res.conf. run-bananads exports BANANADS_RENDER_SCALE from it. run-drastic never reads that file and never patches hires_3d in drastic.cfg (it only swaps threaded_3d and mic).

**Fix.** In run-drastic, read nds-per-game-res.conf and, for this run only, set hires_3d (1 or 0; SuperDrastic has no 3x/4x) the same way threaded_3d is swapped. BananaDS already works.

Files: `ui/src/settings.cpp (nds-per-game-res.conf); system/rootfs/usr/libexec/banana/run-drastic; run-bananads`.

### [116](https://github.com/JorreFog/BananaLinux/issues/116) — Charge LED off when the device is off

OPEN · Power · Open pull request · #123

**Cause.** Same cluster as #34/#99/#8. The amber LED is a PWM on the RK817. Fully off, that rail is down, so no LED even while the charger IC takes current.

**Fix.** PR #123: bananad --charge keeps a tiny on-charger path so the PWM LED can light without a full boot. Confirm it does not undo deep off / drain.

Files: `system/bananad/bananad.c; RK817 PWM LEDs`.

### [114](https://github.com/JorreFog/BananaLinux/issues/114) — Integer scaling + other shaders in BananaDS

OPEN · BananaDS · Cause known; change not in tree

**Cause.** Shader 'none' bilinear-filters on the Plus; LCD shaders run on that filtered image, so they look soft next to RetroArch's integer-then-shader path.

**Fix.** Scale nearest/integer to the panel first, then run the .frag on that image. Add an Integer+LCD (and Integer+lcd3x) preset. Do not bilinear then LCD.

Files: `BananaDS kms.shader; shader pass`.

### [113](https://github.com/JorreFog/BananaLinux/issues/113) — Fast forward on SuperDrastic is R3; Plus has no R3

OPEN · SuperDrastic · Cause known; change not in tree

**Cause.** Stock DraStic mapping uses 1036 (R3). The Plus has no R3, and changing DraStic's own menu mapping can lock the user out of that menu (#113 body).

**Fix.** Ship R2 as the default FF (BananaDS already uses R2). Reset controls_a/b FF if still 1036. Optional: Settings row. Do not send people into DraStic's control editor.

Files: `buildroot/board/rgds/drastic.cfg controls_a[CONTROL_INDEX_FAST_FORWARD] = 1036`.

### [112](https://github.com/JorreFog/BananaLinux/issues/112) — Font issue (capital H)

OPEN · Menu · Cause known; change not in tree

**Cause.** A one-pixel spur on H makes it read as M at the menu's size.

**Fix.** Fix the glyph in the bundled font or the 1-bit raster of that codepoint. Screenshot at native panel size to confirm.

Files: `Pixelify Sans / banana-ui text raster`.

### [111](https://github.com/JorreFog/BananaLinux/issues/111) — Plans about a file manager?

CLOSED · Menu · Shipped, issue still open or already closed · in 0.5.x · #115

**Cause.** Asked whether a file manager was planned.

**Fix.** Files (copy/move/rename/delete) landed. Closed.

Files: `Files shelf / PR #115`.

### [110](https://github.com/JorreFog/BananaLinux/issues/110) — Fast forward sound

OPEN · BananaDS · New emulator, store app, or product choice · in 0.4.0

**Cause.** BananaDS mutes while fast-forwarding (documented with #13). Reporter asks if silence is normal.

**Fix.** Document it in the in-game help. Optional: a quiet chip-tune or pitch-up mix; full audio at Max FF will underrun.

Files: `run-bananads / BananaDS fast-forward`.

### [109](https://github.com/JorreFog/BananaLinux/issues/109) — Homeserver must append :443; no skip-verify

OPEN · Network · Cause known; change not in tree · in 0.5.0

**Cause.** probe() tries host:8092, :8080, :80 (http). It never tries https or :443, so a RomM behind TLS on 443 400s as plain HTTP. No TLS skip-verify either.

**Fix.** If the address has no scheme/port, also try https://host and https://host:443. Add a Settings toggle to skip TLS verify for LAN self-signed certs (off by default).

Files: `ui/src/sync.cpp probe()`.

### [108](https://github.com/JorreFog/BananaLinux/issues/108) — Make cartridge image fit, not fill

OPEN · Menu · Cause known; change not in tree · in 0.5.0

**Cause.** Title screens are drawn cover-style (aspect preserved, edges cropped). Reporter wants contain + white pad, and a choice of box vs title vs icon.

**Fix.** Draw cart art with contain into the label rect; fill leftover with white. A Display row already picks icon vs art (#106); add Box / Title / Icon if still missing.

Files: `ui/rnds cart drawing; ui/src/library.cpp cartArt`.

### [107](https://github.com/JorreFog/BananaLinux/issues/107) — DS cartridge shows icon but resume chip shows title screen

OPEN · Menu · Cause known; change not in tree · in 0.5.0

**Cause.** dsCartIcons fills GameInfo.cartArt / rom for the reel. The top-screen resume chip is filled from SystemInfo resumeBox/Cart/Label and never looks at dsCartIcons, so Last played keeps title/box art.

**Fix.** In Library::system(), when dsCartIcons and the resume game is a DS ROM, set resumeLabel/resumeCart from romIconCover the same way game() does.

Files: `ui/src/library.cpp Library::system() vs Library::game()`.

### [106](https://github.com/JorreFog/BananaLinux/issues/106) — Option to show original cartridge art

CLOSED · Menu · Shipped, issue still open or already closed · in 0.5.0

**Cause.** 0.5.0 defaulted to media/label; the icon/original-cart option was missed.

**Fix.** The option exists. Closed.

Files: `Settings > Display & sound > DS cartridges`.

### [105](https://github.com/JorreFog/BananaLinux/issues/105) — SuperDrastic inverting screens (Spirit Tracks)

OPEN · SuperDrastic · Inside DraStic or BananaDS/melonDS

**Cause.** Spirit Tower swaps the two DS screens in SuperDrastic (touch stays on the bottom). Same class as #67 Dragon Quest with threaded 3D on.

**Fix.** Workaround: X on the game > Threaded 3D Off. Optional: ship known-bad titles with threaded_3d=0. A real fix is inside DraStic.

Files: `DraStic threaded_3d; nds-per-game-3d.conf`.

### [104](https://github.com/JorreFog/BananaLinux/issues/104) — Battery not charging past 95%

OPEN · Power · Open pull request · in 0.5.0 · #123

**Cause.** Off-charge was invisible to the old gauge (jump 90%→100% on boot). LED off while off is the PWM-on-RK817 limit. A 5W brick is also slow.

**Fix.** 0.5.0 voltage floor should stop the stuck-95% lie. LED-while-off is PR #123. Confirm on 0.5.0+ after a long off-charge.

Files: `bananad gauge; RK817 charger; LED PWM`.

### [103](https://github.com/JorreFog/BananaLinux/issues/103) — Add PSP via PPSSPP

OPEN · RetroArch · New emulator, store app, or product choice

**Cause.** No PSP core or shelf in the image.

**Fix.** Add libretro-ppsspp (or standalone), roms/psp, a systems.conf entry, and a control profile. Heavy; RK3566 is marginal for many PSP titles.

Files: `Buildroot libretro cores; systems.conf; roms/psp`.

### [102](https://github.com/JorreFog/BananaLinux/issues/102) — No boot, power LED, or display with ROCKNIXDS or BananaOS

OPEN · Boot · Install / docs / user setup · #91

**Cause.** Two Plus units: stock TF boots, Banana/ROCKNIXDS TF1 does not (no LED). Same class as #58/#83: incomplete image write (Windows error 5 at ~20 MiB skips U-Boot), TF1 vs TF2, or a board that will not take this U-Boot.

**Fix.** Installer must write from offset 0 (sector 64). Recommend Linux/macOS or a current Etcher; compare bytes 32–64KiB to a known-good image. PR #91 is for a failed *update*, not a blank first flash.

Files: `U-Boot at sector 64; firstboot; Windows raw write`.

### [100](https://github.com/JorreFog/BananaLinux/issues/100) — Replace SuperDrastic with dsperate

OPEN · SuperDrastic · New emulator, store app, or product choice

**Cause.** DraStic is closed-source and old. dsperate is a different emulator, not a drop-in.

**Fix.** Keep SuperDrastic as the compatibility option; BananaDS is the open path. A dsperate port is a new emulator contract, not a swap.

Files: `SuperDrastic / DraStic binary`.

### [99](https://github.com/JorreFog/BananaLinux/issues/99) — Device doesn't charge when off

OPEN · Power · Open pull request · in 0.5.0 · #123

**Cause.** The Plus does take charge while off; 0.4.x gauge could ignore it. No LED while off. Reporter still saw no % change until plugging in while on.

**Fix.** 0.5.0 voltage floor + PR #123 LED path. Ask for a 0.5.0+ off-charge test with voltage and % before/after.

Files: `RK817 charger; bananad gauge; PWM LED`.

### [98](https://github.com/JorreFog/BananaLinux/issues/98) — Can DSi emulation be added?

CLOSED · BananaDS · Shipped, issue still open or already closed · in 0.5.0.1

**Cause.** DSi / DSiWare were not runnable.

**Fix.** 0.5.0.1: .dsi/.cia on BananaDS with dumped DSi BIOS. Closed.

Files: `BananaDS + bios/dsi_bios7.bin, dsi_bios9.bin`.

### [97](https://github.com/JorreFog/BananaLinux/issues/97) — SuperDrastic graphical glitches NSMB

OPEN · SuperDrastic · Inside DraStic or BananaDS/melonDS

**Cause.** Sky/water themes tear and go red in SuperDrastic; BananaDS is fine. DraStic's software raster, not Banana's compositor.

**Fix.** Use BananaDS for that title. No OS-side fix without a DraStic update.

Files: `DraStic rasterizer`.

### [96](https://github.com/JorreFog/BananaLinux/issues/96) — SuperDrastic SM64DS not saving

OPEN · SuperDrastic · Cause known; change not in tree

**Cause.** In-game save missing after reload in SuperDrastic; BananaDS fine. Either DraStic wrote nowhere banana-ui looks, the .dsv name does not match the ROM, or nds-saves overwrote a newer .dsv with an empty .sav.

**Fix.** Log nds-saves before/after SM64DS. If sync mtime is wrong, prefer the larger/newer file and never clobber a non-empty .dsv with a 0-byte .sav.

Files: `system/rootfs/usr/libexec/banana/nds-saves; drastic/backup/*.dsv`.

### [95](https://github.com/JorreFog/BananaLinux/issues/95) — SuperDrastic crackling in MapleStory DS

OPEN · SuperDrastic · Inside DraStic or BananaDS/melonDS

**Cause.** Crackle in SuperDrastic only; BananaDS fine. DraStic's audio path when the game stalls.

**Fix.** Use BananaDS, or live with DraStic's mixer. Not the #12 BananaDS hold-last-sample bug (that shipped in 0.4.0).

Files: `DraStic audio`.

### [93](https://github.com/JorreFog/BananaLinux/issues/93) — Developer options with a flight recorder

OPEN · Menu · Open pull request · #94

**Cause.** Logs on the card do not record buttons, lid, or which menu screen was up.

**Fix.** Review/merge PR #94 (off by default, no secrets).

Files: `PR #94`.

### [92](https://github.com/JorreFog/BananaLinux/issues/92) — BananaDS homebrew ALTTP DS port ARM9 abort

OPEN · BananaDS · Inside DraStic or BananaDS/melonDS

**Cause.** lttp-ds.nds boots in desktop melonDS 1.1, ARM9 data-aborts in BananaDS; SuperDrastic dies 132. BANANADS_JIT_ENABLE did not change the log. Secure-area decrypt failed first.

**Fix.** Compare BananaDS's melonDS pin and secure-area/homebrew path to upstream 1.1. Homebrew that needs a different cart header or JIT off may need an explicit setting that actually reaches the core.

Files: `BananaDS / melonDS JIT and secure area`.

### [90](https://github.com/JorreFog/BananaLinux/issues/90) — Fall back to the previous version when an update doesn't start

OPEN · Boot · Open pull request · #91

**Cause.** A bad slot can sit on a black screen with no automatic revert.

**Fix.** Review/merge PR #91: trial boot, then DEFAULT back to the old slot; breadcrumbs on the card.

Files: `extlinux DEFAULT; PR #91`.

### [89](https://github.com/JorreFog/BananaLinux/issues/89) — A shutdown animation to match the boot one

CLOSED · Menu · Shipped, issue still open or already closed · in 0.5.0 · #72

**Cause.** Power-off cut to black.

**Fix.** Merged in 0.5.0. Closed.

Files: `banana-ui shutdown animation`.

### [88](https://github.com/JorreFog/BananaLinux/issues/88) — One wrong line in settings.conf or a theme can crash the menu

OPEN · Menu · Open pull request · #73

**Cause.** Unexpected enums crash at every boot; pad() with an empty filler can hang; long theme formulas can blow the stack. Some lists now terminate with nullptr; pad() is still unbounded on main.

**Fix.** Merge PR #73 (fuzz + default-on-bad-value + bounded pad). Until then, a bad settings.conf line is a brick.

Files: `ui/src/settings.cpp; ui/src/expr.cpp pad(); theme formulas`.

### [87](https://github.com/JorreFog/BananaLinux/issues/87) — Power and volume buttons sometimes don't respond right after turning on

OPEN · Power · Cause known; change not in tree · #86

**Cause.** Menu is up before bananad owns power/volume, so those keys do nothing for a few seconds. PR #86 (closed, unmerged) also wanted a hardware-check toast.

**Fix.** Start bananad's input grab before banana-ui paints, or have banana-ui handle volume/power until bananad takes over. Reopen the useful part of #86.

Files: `bananad vs banana-ui startup; evdev`.

### [85](https://github.com/JorreFog/BananaLinux/issues/85) — Turn off by itself after sleeping for a while

OPEN · Power · Partly in a release; leftover listed · in 0.5.0 · #84

**Cause.** Lid-sleep could drain to empty. Shutdown-with-lid-closed could re-enter sleep (#46).

**Fix.** 0.5.0 shipped the auto-power-off timer. Still missing from #84: LED while asleep, instant black on lid, idle timers in games, plainer help. PR #84 is closed unmerged.

Files: `Settings > Sleep & power > Power off after sleeping; PR #84`.

### [83](https://github.com/JorreFog/BananaLinux/issues/83) — Cannot install BananaOS

OPEN · Boot · Install / docs / user setup

**Cause.** Defender deleted the flasher; Windows error 5 at write; Rufus black screens (U-Boot not written). Same flash-integrity class as #58/#102.

**Fix.** Document: allow the flasher, use a current Etcher/Linux, never a 'partition only' write. Sign/notarize the flasher if Defender keeps eating it. Not an OS runtime bug.

Files: `BananaLinux Flasher; Windows Defender; raw disk write`.

### [82](https://github.com/JorreFog/BananaLinux/issues/82) — BananaDS rendering issue with Pokemon Diamond

OPEN · BananaDS · Inside DraStic or BananaDS/melonDS

**Cause.** 3D collapses to lines on Cycle Road in BananaDS; SuperDrastic plays it. melonDS/Gengis scene bug.

**Fix.** Use SuperDrastic for that stretch, or fix the renderer in BananaDS (related to other Gengis edge bugs).

Files: `BananaDS / Gengis 3D (Cycle Road / Route 206)`.

### [79](https://github.com/JorreFog/BananaLinux/issues/79) — High power usage in sleep

OPEN · Power · Open pull request · in 0.5.0 · #101

**Cause.** Gadget off → DWC3/xHCI stays host-only → xHCI halt -110 → bananad used to stick to s2idle after two failures (~25%/night). 0.5.0 retries deep sleep each time and adds auto-power-off; reporter still saw 11% overnight on 0.5.1.

**Fix.** Merge PR #101: dual-role OTG so the host can halt, and s2idle fallback for that sleep only. Hardware floor is still ~0.5%/h even when suspend works.

Files: `linux-banana.fragment CONFIG_USB_GADGET is not set; bananad suspend; xHCI`.

### [78](https://github.com/JorreFog/BananaLinux/issues/78) — Customize home row

OPEN · Menu · Partly in a release; leftover listed · in 0.5.0

**Cause.** Want hide/reorder PortMaster etc. Deleting the PortMaster folder could refuse to boot.

**Fix.** 0.5.0: hide systems without deleting folders. Still needed: reorder the home row, and a guard so a missing ports folder cannot brick boot.

Files: `Settings > Display & sound > Shown systems`.

### [77](https://github.com/JorreFog/BananaLinux/issues/77) — SuperDrastic hotkeys

OPEN · SuperDrastic · Cause known; change not in tree

**Cause.** Save/load/quit exist inside DraStic's menu, not as Banana F-combos the way BananaDS does.

**Fix.** Map F+L / F+R / F+Start in the SuperDrastic wrapper (or write controls_* from Settings) without opening DraStic's own mapper (#113).

Files: `drastic.cfg F-combos; banana-ui cannot assign them`.

### [76](https://github.com/JorreFog/BananaLinux/issues/76) — Dot Mizai shaders do not render correctly

OPEN · BananaDS · Cause known; change not in tree · in 0.4.2

**Cause.** Dot Mizai assumes stock/ROCKNIX scale and a grid overlay. Banana's shader pass is a different resolution/filter, so the LCD grid is wrong.

**Fix.** Document the .frag contract (pixel size, integer vs bilinear). Add a Plus-native Mizai that matches 1024×768 integer 4×, or run shaders after integer scale (#114).

Files: `DS .frag shader contract vs stock overlay`.

### [75](https://github.com/JorreFog/BananaLinux/issues/75) — Dark second screen panel during single-screen gaming

OPEN · Menu · Partly in a release; leftover listed · in 0.5.0

**Cause.** Light home theme makes the idle game panel bright.

**Fix.** 0.5.0 can dim/turn off the other panel. Still needed: a Dark panel option that ignores the light theme.

Files: `Settings > Other systems > Game panel when idle`.

### [74](https://github.com/JorreFog/BananaLinux/issues/74) — Color correction on both screens

OPEN · Menu · SoC / panel limit

**Cause.** RK VOP2 exposes one GAMMA_LUT. Banana applies it to one CRTC (top, bottom, or none).

**Fix.** Cannot have two independent curves. If the same curve on both is enough, program that LUT on both CRTCs if the hardware allows a shared table; else keep one screen and say so in Settings.

Files: `ui/src/kms.cpp GAMMA_LUT; kms.h: display controller has one table`.

### [71](https://github.com/JorreFog/BananaLinux/issues/71) — Slow gameplay compared with ROCKNIX

OPEN · RetroArch · Partly in a release; leftover listed · in 0.5.0

**Cause.** Threaded video / auto frame delay off; many GLSL presets failed to link so RA fell back. N64/PSX were heavy.

**Fix.** 0.5.0: shaders link again, N64 native res, PSX audio, latency/VSync rows. Still open: whether threaded video should default on, and crt-easymode cost. Need a 0.5.0 comparison list.

Files: `RetroArch cfg; GLSL link on Mali-G52`.

### [70](https://github.com/JorreFog/BananaLinux/issues/70) — Show Linux kernel version

OPEN · Menu · Shipped, issue still open or already closed · in 0.5.0

**Cause.** About had no kernel string.

**Fix.** Shipped in 0.5.0 (kernel + RetroArch versions). Close the issue.

Files: `Settings > System > About`.

### [69](https://github.com/JorreFog/BananaLinux/issues/69) — Download updates on device

OPEN · Network · Shipped, issue still open or already closed · in 0.5.0

**Cause.** Updates were copy-to-card only.

**Fix.** Shipped in 0.5.0 (GitHub OTA). Close the issue.

Files: `libexec/banana/update check|download`.

### [68](https://github.com/JorreFog/BananaLinux/issues/68) — Discord invite link has expired

CLOSED · Boot · Shipped, issue still open or already closed

**Cause.** Invite expired.

**Fix.** Permanent invite. Closed.

Files: `README / website`.

### [67](https://github.com/JorreFog/BananaLinux/issues/67) — Couple of game bugs (DQ, Love Plus, VP, Pokemon outlines)

OPEN · BananaDS · Inside DraStic or BananaDS/melonDS · in 0.4.2

**Cause.** Mix: BananaDS 2x broken in some 3D games, fog/yellow, flashing; DraStic swapped screens / missing 3D unless threaded 3D is off; Pokemon outline geometry is industry-wide.

**Fix.** Per-game Threaded 3D Off for DraStic titles that swap. BananaDS fog/2x are core bugs. Pokemon outlines: do not treat as BananaOS.

Files: `threaded_3d; BananaDS fog/resolution; DraStic screens`.

### [65](https://github.com/JorreFog/BananaLinux/issues/65) — sd2 ROMs not always detected

OPEN · Menu · Shipped, issue still open or already closed · in 0.5.0

**Cause.** A scan could leave the second-card lock held by file sharing, so later boots showed no games until the setting was toggled.

**Fix.** Fixed in 0.5.0. Close after confirmation (reporter moved to one card).

Files: `library scan lock vs ksmbd`.

### [64](https://github.com/JorreFog/BananaLinux/issues/64) — Sleep mode not working as intended

OPEN · SuperDrastic · Partly in a release; leftover listed · in 0.5.0

**Cause.** After ~36 min asleep in SuperDrastic, white unresponsive screens: DraStic treated sleep as a huge frame wait. BananaDS was fine.

**Fix.** 0.5.0 ignores that gap. A different long-sleep freeze (game stuck, in-game menu still works) is still listed as known. Collect whether white screens remain on 0.5.0.

Files: `SuperDrastic frame timer vs CLOCK_MONOTONIC gap`.

### [58](https://github.com/JorreFog/BananaLinux/issues/58) — RG DS — GammaOS on internal storage, SD card doesn't boot

CLOSED · Boot · Install / docs / user setup

**Cause.** Incomplete Windows write left Android bootable; ROCKNIX cards that actually contained U-Boot worked. Flashing on Linux / latest Etcher fixed it.

**Fix.** Closed. Keep the flasher notes with #83/#102.

Files: `U-Boot overwrite; Windows error 5 at 20 MiB`.

### [56](https://github.com/JorreFog/BananaLinux/issues/56) — RetroAchievements menu

OPEN · Menu · Cause known; change not in tree

**Cause.** RA login exists; no index, unlock-sound, hardcore, or achievement browser in the menu.

**Fix.** A Settings > RetroAchievements page: enable, hardcore, sounds, and an optional hash index. Viewing the full set needs RA's API and a lot of UI.

Files: `banana-ui; RetroArch cheevos; ra-secrets`.

### [55](https://github.com/JorreFog/BananaLinux/issues/55) — Allow remapping of L3

OPEN · BananaDS · Cause known; change not in tree

**Cause.** L3 is menu; the physical menu button now exists, so L3 is wasted. Rhythm games want L2/R2 → L1/R1.

**Fix.** BananaDS: a remap table in Settings (and unmap). SuperDrastic: an Unassigned sentinels in drastic.cfg (#52). Do not require DraStic's on-device mapper.

Files: `BananaDS has no remap; DraStic maps but cannot unmap`.

### [54](https://github.com/JorreFog/BananaLinux/issues/54) — Reimplementation of performance overlay

OPEN · BananaDS · Cause known; change not in tree

**Cause.** ROCKNIXDS had FPS / detailed / advanced overlays.

**Fix.** Extend BananaDS's counter (clocks, 3D load). SuperDrastic would need a wrapper overlay; DraStic will not grow one.

Files: `BananaDS FPS; no clocks overlay in SuperDrastic`.

### [53](https://github.com/JorreFog/BananaLinux/issues/53) — Pls remove

CLOSED · Menu · Shipped, issue still open or already closed

**Cause.** Accidental issue.

**Fix.** Closed.

### [52](https://github.com/JorreFog/BananaLinux/issues/52) — Unmap button

OPEN · SuperDrastic · Cause known; change not in tree

**Cause.** DraStic can assign but not clear a key, so accidental save/load stays bound.

**Fix.** Write 0 / none for that control from Settings, or a Reset hotkeys row. Same work as #55.

Files: `drastic.cfg controls_*`.

### [49](https://github.com/JorreFog/BananaLinux/issues/49) — Multiple subjects after using 0.4.1

OPEN · Menu · Partly in a release; leftover listed · in 0.4.2 / 0.5.0

**Cause.** Kitchen-sink: DSi, folders, metadata, grid, rebind F, picker menus, in-game clocks, integer-to-edge, screenshots, RA toggle, play-time reset, OTA; plus in-game menu shader revert and integer touch offset.

**Fix.** Shipped: names/sort, DS name/language, per-game 3D/threaded/mic, OTA, BananaDS shader-behind-menu and integer touch, DSi files. Still open: folders (#121), grid (#120), RA toggle (#56), screenshots, F remap, SMB reliability (comment). Split remaining bullets to their own issues and close this.

Files: `many`.

### [48](https://github.com/JorreFog/BananaLinux/issues/48) — Hardcore RetroAchievements for DS emulators

OPEN · BananaDS · New emulator, store app, or product choice

**Cause.** Hardcore needs official emulator support and no save states.

**Fix.** BananaDS would need an RA hardcore path and to disable states. SuperDrastic is a closed DraStic; unlikely. Apply to RA only after the core can actually enforce it.

Files: `RA integration in BananaDS / SuperDrastic`.

### [47](https://github.com/JorreFog/BananaLinux/issues/47) — RetroAchievements will not sign in after reboot (502)

OPEN · Network · Cause known; change not in tree

**Cause.** Wi-Fi is up and file sharing works; first-time RA login returns 502. 502 is the RA server (or a proxy) refusing; also possible: token not persisted, or login hits HTTP before DNS/NTP.

**Fix.** Retry with backoff; persist the token in banana/accounts.conf; block the login row until time is set. Log the URL and status. If it is only RA 502, surface 'server said 502' and retry.

Files: `ui ra-secrets; RetroAchievements HTTP`.

### [46](https://github.com/JorreFog/BananaLinux/issues/46) — Can't turn off the device (keeps turning on after couple hours)

OPEN · Power · Cause known; change not in tree · in 0.5.0 · #84

**Cause.** A 'shut down' unit is found asleep hours later. Unplug-while-on then Shut down can leave black-but-lit screens. rcK can re-enter sleep mid-shutdown; charger/RTC can look like a wake.

**Fix.** 0.5.0 added a Power menu and shutdown animation (tells sleep from off) but not the main bug. Pause suspend until poweroff completes (the useful part of #84). Trace rcK vs bananad around shutdown.

Files: `rcK shutdown vs lid/sleep; bananad RTC/charger`.

### [45](https://github.com/JorreFog/BananaLinux/issues/45) — DS firmware settings

OPEN · BananaDS · Partly in a release; leftover listed · in 0.4.2

**Cause.** Want remaps, name, colour, birthday, region, language.

**Fix.** 0.4.2: DS name and language. Still needed: favourite colour, birthday, region, L2/R2 maps (#55).

Files: `Settings > Nintendo DS`.

### [42](https://github.com/JorreFog/BananaLinux/issues/42) — Add DSiWare support

OPEN · BananaDS · Partly in a release; leftover listed · in 0.5.0.1

**Cause.** DSiWare did not run on other RG DS firmwares either.

**Fix.** 0.5.0.1 runs .dsi/.cia with dumped BIOS. Keep open only for titles that still fail (e.g. Risky Revenge); otherwise close as duplicate of #98.

Files: `BananaDS DSi mode + BIOS`.

### [41](https://github.com/JorreFog/BananaLinux/issues/41) — Specific menu items slow to load

OPEN · Menu · Cause known; change not in tree

**Cause.** Those rows hitch 15s+: they walk large lists or the card on the UI thread.

**Fix.** Build the timezone/theme/scraper lists off-thread (or once at boot) and keep the last result. Never scan the card inside the row's draw/open.

Files: `settings Time zone / Themes / Box art From`.

### [40](https://github.com/JorreFog/BananaLinux/issues/40) — Connecting to a saved Wi-Fi network asks for the password again

OPEN · Network · Shipped, issue still open or already closed · in 0.4.2 · #43

**Cause.** A on a saved SSID always opened the keyboard.

**Fix.** Fixed in 0.4.2 (A joins with the stored PSK). Close; PR #43 may be leftover.

Files: `Settings > Network`.

### [39](https://github.com/JorreFog/BananaLinux/issues/39) — Zero-copy panels for SDL_BANANA_PANELS

OPEN · Menu · Shipped, issue still open or already closed · in 0.4.2

**Cause.** GBM read-back was ~8 fps for LÖVE; zero-copy dual-CRTC viewport ~60 fps.

**Fix.** Patch is in 0.4.2. Close.

Files: `buildroot/patches/sdl2/`.

### [38](https://github.com/JorreFog/BananaLinux/issues/38) — Add a YouTube player

OPEN · Feature · New emulator, store app, or product choice

**Cause.** ROCKNIXDS has a YouTube DS app. Not part of the squashfs.

**Fix.** A Store app, not a core package (codec, ToS, network). Point at PortMaster/Store.

Files: `Store / PortMaster`.

### [37](https://github.com/JorreFog/BananaLinux/issues/37) — BananaDS FPS counter wrong

CLOSED · BananaDS · Shipped, issue still open or already closed · in 0.4.2

**Cause.** Counter showed emu 60 while the game was slower.

**Fix.** Fixed in 0.4.2 (game FPS vs emu FPS).

Files: `BananaDS FPS`.

### [36](https://github.com/JorreFog/BananaLinux/issues/36) — Not updating to 0.4.1 (rootfs.squashfs hash)

OPEN · Boot · Shipped, issue still open or already closed · in 0.4.2

**Cause.** A damaged package failed the squashfs hash with a terse error.

**Fix.** 0.4.2 retries unpack and explains a bad SHA. Reporter got onto 0.4.2. Close.

Files: `libexec/banana/update apply`.

### [35](https://github.com/JorreFog/BananaLinux/issues/35) — Missing charging battery icon

OPEN · Menu · Partly in a release; leftover listed · in 0.4.2 / 0.5.0

**Cause.** No bolt on the home battery while charging; then missing in the in-game menu.

**Fix.** 0.4.2 home bolt; 0.5.0 pause menus. Reporter still sees no bolt in the BananaDS menu — finish that path.

Files: `home battery; in-game pause menus`.

### [34](https://github.com/JorreFog/BananaLinux/issues/34) — RG DS Plus doesn't charge when it's turned off

OPEN · Power · Open pull request · in 0.5.0 · #123

**Cause.** Head of the charge-while-off cluster: charge happens, gauge lied, LED cannot light with RK817 down. Stock firmware keeps a small charge UI.

**Fix.** 0.5.0 gauge floor. PR #123 for the LED. Keep this as the tracker until both are confirmed on hardware.

Files: `RK817; gauge; PWM LED`.

### [33](https://github.com/JorreFog/BananaLinux/issues/33) — Toy Story 3 dialogue stutters in BananaDS 0.4.1

OPEN · BananaDS · Partly in a release; leftover listed · in 0.4.2

**Cause.** Skipping dialogue stalled the emu because saves ran on the emu thread.

**Fix.** 0.4.2 writes saves off-thread. Needs a 0.4.2+ confirm on Toy Story 3; close if gone.

Files: `BananaDS save thread`.

### [31](https://github.com/JorreFog/BananaLinux/issues/31) — Transferring existing save files to BananaOS

CLOSED · SuperDrastic · Shipped, issue still open or already closed

**Cause.** Saves in the wrong folder/name were ignored.

**Fix.** Documented. Closed.

Files: `nds-saves; saves/nds/*.sav; drastic/backup/*.dsv`.

### [30](https://github.com/JorreFog/BananaLinux/issues/30) — DraStic username in initial setup

OPEN · Menu · Shipped, issue still open or already closed · in 0.4.2

**Cause.** Firmware name was Dr Drastic.

**Fix.** Shipped in 0.4.2. Close.

Files: `Settings > Nintendo DS > DS name; first-time setup`.

### [29](https://github.com/JorreFog/BananaLinux/issues/29) — Allow editing game metadata

OPEN · Menu · Partly in a release; leftover listed · in 0.4.2

**Cause.** Want display name + sort title that survive scrape.

**Fix.** 0.4.2 added both. Leftover: ROCKNIX <name> vs <sortname> — Banana sorts but still shows the original title until Name is edited.

Files: `X > Name / Sort title; gamelist.xml sortname`.

### [28](https://github.com/JorreFog/BananaLinux/issues/28) — Allow manual title matching when scraping

OPEN · Menu · Partly in a release; leftover listed · in 0.4.2

**Cause.** Auto-scrape misses translations and hacks.

**Fix.** 0.4.2: type a title and pick a match. Leftover: preview art/description before apply.

Files: `X > Box art search`.

### [27](https://github.com/JorreFog/BananaLinux/issues/27) — Add per-game configuration in menu

OPEN · Menu · Partly in a release; leftover listed · in 0.4.2 / 0.5.0

**Cause.** Want per-game threaded 3D, mic, shader, 3D res.

**Fix.** 0.4.2: emulator/shader/threaded/mic. 0.5.0: 3D res row — but SuperDrastic ignores it (#117). Close this when #117 is done.

Files: `X on a DS game; nds-per-game-*.conf`.

### [26](https://github.com/JorreFog/BananaLinux/issues/26) — Expose additional DraStic settings in menu

OPEN · SuperDrastic · Shipped, issue still open or already closed · in 0.4.2

**Cause.** Threaded 3D was only inside DraStic.

**Fix.** Shipped (global + per game). Close.

Files: `Settings > Nintendo DS > Threaded 3D`.

### [25](https://github.com/JorreFog/BananaLinux/issues/25) — Expose internal and external storage through SMB

OPEN · Network · Partly in a release; leftover listed · in 0.4.2

**Cause.** Only \\ip\banana, so a games-on-sd2 setup was invisible. Also ROCKNIX .dsv next to ROMs.

**Fix.** 0.4.2: \\ip\sd2 and first-time .dsv import. Leftover: Windows tree connect / signing / 60s notify (#25 comments); optional \\ip\update alias.

Files: `ksmbd banana + sd2 shares`.

### [24](https://github.com/JorreFog/BananaLinux/issues/24) — Configurable credentials for SMB

OPEN · Network · Partly in a release; leftover listed · in 0.4.2

**Cause.** Windows refused guest SMB.

**Fix.** 0.4.2: banana/linux by default. Leftover: SSH on Windows still awkward (dropbear auth).

Files: `Settings > Network > Sharing name/password`.

### [23](https://github.com/JorreFog/BananaLinux/issues/23) — Move SSH to Settings → Network

OPEN · Menu · Shipped, issue still open or already closed · in 0.4.2

**Cause.** SSH lived under System.

**Fix.** Moved in 0.4.2. Close.

Files: `Settings > Network`.

### [22](https://github.com/JorreFog/BananaLinux/issues/22) — Select emulated language

CLOSED · BananaDS · Shipped, issue still open or already closed · in 0.4.2

**Cause.** No DS firmware language.

**Fix.** Shipped. Closed.

Files: `Settings > Nintendo DS > DS language`.

### [21](https://github.com/JorreFog/BananaLinux/issues/21) — UTC-5 Lima, Bogota and other Latam timezones

CLOSED · Menu · Shipped, issue still open or already closed · in 0.4.2

**Cause.** 30 zones, no Latam.

**Fix.** 49 zones in 0.4.2. Closed.

Files: `Settings > System > Time zone`.

### [20](https://github.com/JorreFog/BananaLinux/issues/20) — BananaDS audio pitch changes on 0.4.0

CLOSED · BananaDS · Shipped, issue still open or already closed · in 0.4.1

**Cause.** 0.4.0 stretched pitch with speed.

**Fix.** 0.4.1 keeps pitch. Closed.

Files: `BananaDS audio servo`.

### [19](https://github.com/JorreFog/BananaLinux/issues/19) — Box art for DS games on the second SD

CLOSED · Menu · Shipped, issue still open or already closed · in 0.4.2

**Cause.** Box art scanned only the first card.

**Fix.** Fixed in 0.4.2. Closed.

Files: `scraper / sd2 paths`.

### [18](https://github.com/JorreFog/BananaLinux/issues/18) — Volume below 100% sounds extremely low

OPEN · Audio · Shipped, issue still open or already closed · in 0.4.2

**Cause.** Linear ALSA % is not loudness; 50% sounded almost mute.

**Fix.** 0.4.2 uses an ear curve on both boards. Close if 0.4.2+ still sounds right; otherwise retune the table.

Files: `bananad volume curve`.

### [17](https://github.com/JorreFog/BananaLinux/issues/17) — Degraded BananaDS performance & pitch on 0.4.0

CLOSED · BananaDS · Shipped, issue still open or already closed · in 0.4.1

**Cause.** Stutters + pitch on Plus after 0.4.0.

**Fix.** Fixed in 0.4.1. Closed.

Files: `BananaDS 0.4.0 regression`.

### [15](https://github.com/JorreFog/BananaLinux/issues/15) — More shaders / add shaders

CLOSED · BananaDS · Shipped, issue still open or already closed · in 0.4.0

**Cause.** No place to add LCD/CRT shaders.

**Fix.** 0.4.0: card .frag files + RA presets. Closed (quality leftover is #76/#114).

Files: `banana/shaders/*.frag; RetroArch GLSL`.

### [14](https://github.com/JorreFog/BananaLinux/issues/14) — Microphone does not seem to work in games

OPEN · BananaDS · Partly in a release; leftover listed · in 0.4.2 / 0.5.0.1

**Cause.** Volume used to be the capture gain (silence at 0). Then BananaDS delivered mic ~1.3 s late. DraStic has only a blow *key*, not PCM. Spirit Tracks still fails for a reporter on 0.5.0.1 even with gate off.

**Fix.** Shipped: capture gain unlinked from volume; 0.5.0.1 ~50 ms path; Phantom Hourglass candles work in tests. Still open: Spirit Tracks songs on Plus — capture BananaDS mic logs vs that game's sampling, and document that SuperDrastic cannot do voice.

Files: `rk817 capture vs volume; BananaDS mic latency; DraStic blow key`.

### [13](https://github.com/JorreFog/BananaLinux/issues/13) — BananaDS is missing fast forward

CLOSED · BananaDS · Shipped, issue still open or already closed · in 0.4.0

**Cause.** No FF.

**Fix.** 0.4.0: hold R2. Closed (mute is #110).

Files: `R2 / F+X`.

### [12](https://github.com/JorreFog/BananaLinux/issues/12) — Light audio crackling with BananaDS

CLOSED · BananaDS · Shipped, issue still open or already closed · in 0.4.0

**Cause.** Gaps when the game ran under 60.

**Fix.** 0.4.0 stretches instead of holding. Closed (SuperDrastic crackle is #95).

Files: `BananaDS audio hold-last-sample`.

### [11](https://github.com/JorreFog/BananaLinux/issues/11) — Antialiasing / deeper graphics settings for BananaDS

CLOSED · BananaDS · Shipped, issue still open or already closed · in 0.4.0

**Cause.** Soft look on the Plus; ROCKNIX had integer scale.

**Fix.** 0.4.0 Sharp pixels (true 4× on 1024×768). Closed.

Files: `Settings > Nintendo DS > Shader > Sharp pixels`.

### [10](https://github.com/JorreFog/BananaLinux/issues/10) — Joystick isn't mapped in BananaDS

CLOSED · BananaDS · Shipped, issue still open or already closed · in 0.4.0

**Cause.** Plus left stick did nothing.

**Fix.** 0.4.0: stick as d-pad. Closed.

Files: `BananaDS input`.

### [9](https://github.com/JorreFog/BananaLinux/issues/9) — No overlay for BananaDS

CLOSED · BananaDS · Shipped, issue still open or already closed · in 0.4.0

**Cause.** Anbernic button did not open a pause menu.

**Fix.** 0.4.0: F opens BananaDS's menu. Closed.

Files: `F / L3 menu`.

### [8](https://github.com/JorreFog/BananaLinux/issues/8) — Charging LED in sleep

OPEN · Power · Partly in a release; leftover listed · in 0.4.2 · #123

**Cause.** PWM LED dies if the RK817 suspends. Sleep-on-charger used to suspend and the light went out.

**Fix.** 0.4.2: any sleep on charger stays dark-and-awake so amber stays on. Fully *off* is still dark (#34/#123). Close #8 if sleep-on-charger is confirmed; leave off-charge to #34.

Files: `bananad dark-awake on charger; PWM LED`.


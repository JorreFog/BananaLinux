# Roadmap

Banana for the **Retroid Pocket Classic** (Qualcomm Snapdragon G1 Gen 2 / SM4450): one 1240×1080 panel, no lid,
OpenABL in place of U-Boot. What follows is this build's own plan; `docs/RPC-PORT.md` is the port's running log
and `docs/RPC-TEST.md` the list to check on the handheld.

## 0.4.2: the first Retroid Pocket Classic build (this branch)

- [x] The whole system boots end to end in QEMU (`tools/qemu-boot.sh`, the CI's `boot` job): the first boot's
      partitioning, the menu, a game started from the library, an A/B update, a shutdown
- [x] A full image build: the CI's `image` job builds `banana-retroid-pocket-classic.img.gz` and its update package,
      and its `boot` job runs the real image through the QEMU test
- [x] The single 1240×1080 panel up from banana-ui, turned 90° (the device tree's `rotation`, `Display::turn()`)
- [x] Buttons and their mapping through the MCU's `rsinput` (A = east, B = south: a Nintendo layout)
- [x] Sound: the WCD937x codec and the two AW88166 amplifiers through the AudioReach DSP, the route published by
      bananad and re-asserted before every open, headphones following the jack
- [x] The GPU: the Adreno a613 through Mesa's freedreno (the kernel catalog and Mesa both told it is a 613)
- [x] Updates: two root slots, a package per build, written to the slot not in use, checked, `Previous version`
- [x] The first-time setup after the install (Wi-Fi, the time zone, a RetroAchievements account, a ScreenScraper
      account); the menu's settings, the burn-in guard and the low-battery notice
- [x] Wi-Fi (`ath10k_snoc`), file sharing (ksmbd, off by default) and SSH (dropbear, keys only)
- [x] No Nintendo DS or 3DS: one panel, and the DS launchers were another handheld's two-panel, performance-tuned
      work; the RetroArch shelves are the whole library

## Next

- [x] Hardware bring-up, then the list in `docs/RPC-TEST.md`: the panel upright and sharp, brightness and sleep,
      sound and headphones, the buttons, the fan and heat, the card, an update. **In progress** on the units.
- [x] Everything RetroArch draws comes out upright: the games run under **cage** (a kiosk Wayland compositor: one
      application, fullscreen, MIT), which turns the output with the connector's "panel orientation" once for the
      game, RetroArch's own menu, its OSD and its widgets. `run-retroarch` starts it with `video_rotation = 0` and
      the Wayland context driver; the per-driver rotation patches are inert under it. The compositor also cost a
      suspend: libseat's VT-bound seat and bananad's `SIGSTOP` of the game deadlocked the kernel's VT switch, fixed
      with `SEATD_VTBOUND=0`
- [ ] PipeWire as the sound server (built and tried once, see `docs/RPC-PORT.md`): the card needs a UCM2 profile for
      this SoC's machine driver before it can own the route, and the image needs the libudev-zero/udev-virtual work
      the attempt made
- [ ] Sleep on the unit: measure the wake (`sleep_debug = yes`) and cut it — the panel's power-on sequence, the SD
      card's re-initialisation (MMC_CAP_RUNTIME_RESUME would take it off the wake path), whether Wi-Fi can stay
      loaded; and standby drain over a night
- [ ] Wi-Fi firmware: the WCN3950's `ath10k` board/firmware files, if any are still missing
- [ ] The fan curve under a long game, and the thermals after ten minutes of N64/PSX

## Later

- [ ] The rest of the Chinese translation (lines added since it was made) and a string extractor for new languages
- [ ] EmulationStation as an alternative frontend
- [ ] RetroAchievements progress on the shelf
- [x] Turnip (the Vulkan driver) beside freedreno: Mesa's freedreno Vulkan, chosen by SwanStation's own renderer
      option; 60 fps at 5x on the a613 (see `docs/RPC-PORT.md`)

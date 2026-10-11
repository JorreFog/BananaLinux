# Roadmap

## 0.1: first boot on the hardware

- [ ] A full image build (`make rgds`, or the CI's `image` job), and the first boot on an RG DS and an RG DS Plus
- [ ] Both panels up from banana-ui; touch on the bottom panel where the finger is
- [ ] Buttons and their mapping (A = east, B = south: the RG DS's Nintendo layout)
- [ ] Suspend and resume: the panels, sound, Wi-Fi, the lid, the power key; standby drain over a night
- [ ] The display handover to SuperDrastic and back (`DSFLIP_HANDOVER=none`)
- [ ] Sound on the units: the controls bananad assumes (`Playback Mux`, `Master`, the aw88166's `PCM`), the AW88166
      amplifier's firmware, the headphone jack's switch on the RG DS Plus
- [ ] The RG DS Plus's real pixel clock and banana-panel-tune's result (`banana/panel-tune.txt`)
- [ ] Headphones plugged in during a game: move the playing stream too (now: from the next time a program opens sound)
- [ ] Boot time, measured: U-Boot, kernel, init, first frame

## 0.2: a handheld you can use every day

- [x] A settings screen in the menu (Start): theme, brightness, motion, sounds, clock, boot animation, sleep, lid,
      LEDs, DS renderer / shader / 3D resolution, Wi-Fi on/off, About, restart, shut down
- [ ] Settings still to come: choosing a Wi-Fi network (an on-screen keyboard), the time zone, volume
- [ ] RetroArch and the libretro cores for the other systems (Buildroot packages), set up for one panel
- [ ] The Favorites shelf refreshing as soon as a game is starred (now on the next scan)
- [ ] Input hotplug in banana-ui (USB and Bluetooth pads)
- [ ] A low-battery warning before the shutdown
- [ ] Updates: a new root squashfs written to an inactive slot from the menu, kept atomic

## Later

- [ ] Kernel trimming for size and boot time (initcall_debug on the device first)
- [ ] The ROM's own icon on cartridges inside `.zip` files (the engine supports it; banana-ui needs a zip reader)
- [ ] RetroAchievements progress on the shelf
- [ ] An SSH/Samba service you can switch on, off by default

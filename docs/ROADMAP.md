# Roadmap

## 0.1: first boot on the hardware

- [ ] A full image build (`make rgds`, or the CI's `image` job), and the first boot on an RG DS and an RG DS Plus
- [ ] Both panels up from banana-ui; touch on the bottom panel where the finger is
- [ ] Buttons and their mapping (A = east, B = south: the RG DS's Nintendo layout)
- [ ] Suspend and resume on the units: the panels, sound, Wi-Fi, the lid, the power key, the RTC battery check;
      standby drain over a night
- [ ] Wake-up time measured (`sleep_debug = yes`), then cut: the panels' power-on delays on the RG DS, the SD card's
      re-initialisation (MMC_CAP_RUNTIME_RESUME would take it off the wake path), whether Wi-Fi can stay loaded
- [ ] The display handover to SuperDrastic and back (`DSFLIP_HANDOVER=none`)
- [ ] Sound on the units: the controls bananad assumes (`Playback Mux`, `Master`, the aw88166's `PCM`), the AW88166
      amplifier's firmware, the headphone jack's switch on the RG DS Plus
- [ ] The RG DS Plus's real pixel clock and banana-panel-tune's result (`banana/panel-tune.txt`)
- [ ] Headphones plugged in during a game: move the playing stream too (now: from the next time a program opens sound)
- [ ] Boot time, measured: U-Boot, kernel, init, first frame

## 0.2: a handheld you can use every day

- [x] A settings screen in the menu (Start): theme, brightness, motion, sounds, clock, boot animation, sleep, lid,
      LEDs, DS renderer / shader / 3D resolution, Wi-Fi on/off, About, restart, shut down
- [x] The time zone (POSIX rules for 30 regions, summer time included; the menu, the games and bananad's logs)
- [x] Choosing a Wi-Fi network: the networks around (signal, lock, saved), the password on an on-screen keyboard
      (buttons or touch), WPA2 and WPA3; forgetting one
- [ ] Settings still to come: volume
- [x] RetroArch and the libretro cores for the other systems (Buildroot packages), on one panel (settings: top or bottom)
- [x] The Favorites shelf and the favourite systems' order updated as soon as the shelf shows again
- [x] Input hotplug in banana-ui (USB and Bluetooth pads, watched with inotify)
- [x] A low-battery warning before the shutdown (a notice over the top panel at 20%, 10% and 5%)
- [ ] Updates: a new root squashfs written to an inactive slot from the menu, kept atomic

## Later

- [ ] Kernel trimming for size and boot time (initcall_debug on the device first)
- [x] The ROM's own icon on cartridges inside `.zip` files (banana-ui's streaming zip reader, zlib)
- [ ] RetroAchievements progress on the shelf
- [ ] An SSH/Samba service you can switch on, off by default

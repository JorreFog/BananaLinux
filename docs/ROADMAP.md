# Roadmap

## 0.1: first boot on the hardware

- [ ] A full image build (`make rgds`, or the CI's `image` job), and the first boot on an RG DS and an RG DS Plus
- [ ] Both panels up from banana-ui; touch on the bottom panel where the finger is
- [ ] Buttons and their mapping (A = east, B = south: the RG DS's Nintendo layout)
- [ ] Suspend and resume: the panels, sound, Wi-Fi, the lid, the power key; standby drain over a night
- [ ] The display handover to SuperDrastic and back (`DSFLIP_HANDOVER=none`)
- [ ] Sound: the mixer control for the volume keys (`bananad.conf`'s `mixer`), the AW88166 amplifier's firmware, and
      ROCKNIX's ALSA UCM profiles for the RG DS Plus (`/usr/share/alsa/ucm2/Anbernic/RG-DS-Plus`)
- [ ] Boot time, measured: U-Boot, kernel, init, first frame

## 0.2: a handheld you can use every day

- [ ] A settings screen in the menu (Start): theme, sounds, sleep, Wi-Fi networks, brightness, the clock, time zone
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

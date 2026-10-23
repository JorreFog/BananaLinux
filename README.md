<p align="center">
  <img src="docs/img/readme/banner.png" width="100%" alt="BananaOS: a light Linux for the Anbernic RG DS and RG DS Plus. Alpha 0.4.2. Next to the title, an RG DS Plus open, its top panel showing the Nintendo DS's stats (8 games, 7 played, 8h 15m, last played Pokémon Black Version 2) and its bottom panel the system shelf with the Nintendo DS selected">
</p>

<p align="center">
  <a href="https://github.com/JorreFog/BananaLinux/releases/tag/v0.4.2">Download 0.4.2</a> ·
  <a href="#get-it">Install</a> ·
  <a href="https://github.com/jesussaves-ai/banana-linux-flasher">Flasher</a> ·
  <a href="#seen-on-youtube">Videos</a> ·
  <a href="https://jorrefog.github.io/BananaLinux/save-mover/">Save Mover</a> ·
  <a href="https://discord.gg/FxeE7f8r">Discord</a>
</p>

<p align="center">
  <a href="https://github.com/JorreFog/BananaLinux/releases"><img src="https://img.shields.io/github/v/release/JorreFog/BananaLinux?include_prereleases&style=for-the-badge&label=release&color=F0B429&labelColor=17161C" alt="Latest release"></a>
  <a href="https://github.com/JorreFog/BananaLinux/releases"><img src="https://img.shields.io/github/downloads/JorreFog/BananaLinux/total?style=for-the-badge&label=downloads&color=234346&labelColor=17161C" alt="Downloads"></a>
  <img src="https://img.shields.io/badge/handhelds-RG%20DS%20%C2%B7%20RG%20DS%20Plus-F1EDE2?style=for-the-badge&labelColor=17161C" alt="For the RG DS and RG DS Plus">
  <a href="https://discord.gg/FxeE7f8r"><img src="https://img.shields.io/badge/discord-join-F0B429?style=for-the-badge&logo=discord&logoColor=F1EDE2&labelColor=17161C" alt="Discord"></a>
</p>

<p align="center">
  <img src="docs/img/readme/stats.png" width="100%" alt="3.9 s from the kernel to the menu (RG DS; 5.9 s on the RG DS Plus), 0.6 s from A to a DS game, DS games at 60 FPS at 2x, 10 systems">
</p>

> [!WARNING]
> BananaOS is an alpha (0.4.2). It runs on the RG DS Plus and the RG DS, every day, on the maintainer's own
> handhelds, but things are still missing and some are rough (see [Status](#status)). If something goes wrong,
> [docs/FIRST-BOOT.md](docs/FIRST-BOOT.md) says which logs to send back.

> [!NOTE]
> Made with AI. BananaOS is developed with the help of AI: much of the code, the tests and the documentation
> was written with Claude (Anthropic) as a coding assistant, directed, reviewed and tested on real RG DS and RG DS
> Plus hardware by the project's maintainer. Bug reports and contributions from people are just as welcome.

> [!TIP]
> 新功能：简体中文。 Settings > System > Language. Translated by Majirefy: thank you!

> [!TIP]
> New: the [BananaLinux Flasher](https://github.com/jesussaves-ai/banana-linux-flasher) by jesussaves-ai puts BananaOS on a card in three clicks.
> See [Get it](#get-it).

<a id="what-it-is"></a>
<img src="docs/img/readme/h-what.png" width="100%" alt="01: What it is. A whole system for two handhelds, built with Buildroot">

BananaOS is a whole system for the RG DS and the RG DS Plus, and nothing else: a mainline kernel with
[ROCKNIX](https://rocknix.org)'s support for the two handhelds, busybox init, and one program on the panels. There
is no systemd, no compositor, no sound server, no EmulationStation and no desktop. The root filesystem is a
read-only squashfs of a few tens of megabytes; everything you add (games, saves, settings) goes on an exFAT
partition any computer can open.

- Fast to start: from the kernel to the menu in 3.9 s on the RG DS and 5.9 s on the RG DS Plus. A DS game
  is on the panels 0.6 s after you press A, and back in the menu a quarter of a second after you quit.
- Light while you play: the menu closes the panels, the buttons and the sound before a game starts, so the game
  has the handheld to itself. Every system gets its own clock profile; the menu runs at the lowest clocks.
- A clamshell that sleeps like one: close the lid and the game pauses, the card is flushed and the handheld
  suspends. Open it and the game carries on as the panels light up. In a DS game the lid is the DS's lid too.
- Updates in place: drop one file in `banana/updates` and pick it in Settings. It goes to the card's second
  system slot, and the version you had stays one restart away.

<a id="a-tour"></a>
<img src="docs/img/readme/h-tour.png" width="100%" alt="02: A tour. Grabbed from the panels of an RG DS Plus running 0.4.2">

<p align="center">
  <img src="docs/img/boot-animation.gif" width="300" alt="The boot animation on both panels: a pixel-art bunch of bananas drops onto the top screen and BANANAOS types itself in under it, while on the bottom screen a cartridge with the bunch on its label slides into the console, which clicks and lights up green">
</p>
<p align="center"><sub>The boot animation: the bunch drops in, and the BANANA cartridge clicks into the back of a DSi XL.</sub></p>

<table>
  <tr>
    <td align="center" width="33%"><img src="docs/img/readme/shot-home.png" alt="Home: the Nintendo DS on the system shelf (bottom), its 8 games, 7 played and 8h 15m on the top panel with the last game played"><br><sub>Home. The system shelf, with your stats per system.</sub></td>
    <td align="center" width="33%"><img src="docs/img/readme/shot-library.png" alt="The DS library: Pokémon Black Version 2's box art, genre and year, 45 plays, 1h 47m and 6 of 187 achievements on the top panel; the games as cartridges on the bottom"><br><sub>The library. Box art, play time and RetroAchievements progress.</sub></td>
    <td align="center" width="33%"><img src="docs/img/readme/shot-options.png" alt="A game's options: Emulator BananaDS, Shader, Threaded 3D, Microphone and Name, with the selected row explained on the top panel"><br><sub>Per game. X picks the emulator, shader, 3D and mic for one game.</sub></td>
  </tr>
  <tr>
    <td align="center"><img src="docs/img/readme/shot-game.png" alt="Mario Kart DS's title screen across both panels"><br><sub>In a game. Each DS screen straight onto its own panel.</sub></td>
    <td align="center"><img src="docs/img/readme/shot-game-menu.png" alt="BananaDS's in-game menu: Resume, Save, Load, Quick settings, Restart game and Quit game as tiles on the bottom panel; the game's session, last save and achievements on the top"><br><sub>The in-game menu. F or L3: states, quick settings, quit.</sub></td>
    <td align="center"><img src="docs/img/readme/shot-store.png" alt="The App Store: eReaderDS with an update from 1.27.3 to 1.27.4 and its notes on the top panel; the Apps tab with eReaderDS and Bank & Trade on the bottom"><br><sub>The App Store. Apps installed and updated from the menu.</sub></td>
  </tr>
  <tr>
    <td align="center"><img src="docs/img/readme/shot-settings.png" alt="Settings: six groups as tiles (Display & sound, Sleep & power, Nintendo DS, Other systems, Network, System); the selected group's contents on the top panel"><br><sub>Settings. Six groups; L and R jump between them.</sub></td>
    <td align="center"><img src="docs/img/readme/shot-light.png" alt="The light theme: the home screen in warm paper tones"><br><sub>Light theme. And themes of your own, from a JSON file.</sub></td>
    <td align="center"><img src="docs/img/readme/shot-carts.png" alt="The Carts theme: the DS games as 3D cartridges on a shelf, Mario Kart DS selected, its stats on the top panel"><br><sub>Carts (experimental): your games as cartridges on a shelf.</sub></td>
  </tr>
  <tr>
    <td align="center"><img src="docs/img/readme/shot-zh-home.png" alt="The home screen in Simplified Chinese"><br><sub>简体中文, translated by Majirefy.</sub></td>
    <td align="center"><img src="docs/img/readme/shot-zh-settings.png" alt="Settings in Simplified Chinese: 显示与声音, 睡眠与电源, Nintendo DS, 其他系统, 网络, 系统"><br><sub>Settings, the setup and both DS emulators' menus too.</sub></td>
    <td align="center"><img src="docs/img/readme/shot-nds.png" alt="Settings > Nintendo DS: Emulator BananaDS, Quick resume, DS language, DS name, Shader LCD grid 2x"><br><sub>Nintendo DS settings. Emulator, shader, DS language and name.</sub></td>
  </tr>
</table>

<a id="seen-on-youtube"></a>
<img src="docs/img/readme/h-videos.png" width="100%" alt="03: Seen on YouTube. Two creators took it for a spin">

<table>
  <tr>
    <td align="center" width="50%">
      <a href="https://www.youtube.com/watch?v=yW6LavgyhL4&t=948s"><img src="docs/img/readme/video-yW6LavgyhL4.png" alt="Retro Game Corps: Custom Firmware on the Anbernic RG DS Plus. Watch on YouTube"></a><br>
      <sub><a href="https://www.youtube.com/@RetroGameCorps">Retro Game Corps</a> puts BananaOS next to GammaOS on the RG DS Plus (BananaOS from 15:48).</sub>
    </td>
    <td align="center" width="50%">
      <a href="https://www.youtube.com/watch?v=KPENdDlkI5M"><img src="docs/img/readme/video-KPENdDlkI5M.png" alt="forthenext: ANBERNIC RG DS Plus, BananaOS and your games tested. Watch on YouTube"></a><br>
      <sub><a href="https://www.youtube.com/@forthenext">forthenext</a> tests your requested games on BananaOS.</sub>
    </td>
  </tr>
</table>
<p align="center"><sub>Thank you both! Made a video about BananaOS? Tell us on <a href="https://discord.gg/FxeE7f8r">Discord</a>.</sub></p>

<a id="nintendo-ds"></a>
<img src="docs/img/readme/h-emulators.png" width="100%" alt="04: Nintendo DS. Two emulators, both straight onto the panels">

<p align="center">
  <img src="docs/img/readme/emulators.png" width="100%" alt="The two DS emulators side by side. SuperDrastic, DraStic on both panels: DraStic by Exophase with each DS screen on its own panel, 2x with Gengis Engine 3D, a per-game CPU governor, an in-game menu with save states, shaders and settings; the fastest of the two. BananaDS, melonDS's core, tuned: melonDS's core on Banana's display path and menu, RetroAchievements and online play through Nintendo WFC, microphone, cheats and fast-forward, speed-ups checked frame by frame against melonDS; the most accurate of the two. Pick one in Settings > Nintendo DS, or per game with X">
</p>

Both put each DS screen straight onto its own panel at 2x, with no compositor in between, and both have the
in-game menu on F (or L3): save and load with pictures, quick settings, shaders, quit. The Anbernic button always
opens it, whatever the buttons are set to.

| | [SuperDrastic](https://github.com/JorreFog/SuperDrastic) | BananaDS |
|---|---|---|
| **Core** | DraStic by Exophase | melonDS's core, in the SuperDrastic repository |
| **Best at** | Speed: the lightest on the CPU | Accuracy, and everything melonDS can do |
| **3D** | Gengis Engine at 2x, threaded | Gengis Engine at 2x, threaded |
| **Clocks** | Per-game governor | Governor that only steps down when the heaviest frames still fit |
| **RetroAchievements** | — | Softcore, with the setup's account |
| **Online** | — | Nintendo WFC through Kaeru, nothing to set up |
| **Microphone** | A "blow" key | The real mic (Nintendogs learns its puppy's name) |
| **Extras** | Shaders, states with pictures | Cheats for 4,000+ games, fast-forward (R2), FPS counter, screenshots (F + L2) |

More in [docs/EMULATORS.md](docs/EMULATORS.md), the contract between the menu and any emulator.

<a id="and-the-rest"></a>
<img src="docs/img/readme/h-systems.png" width="100%" alt="05: And the rest. RetroArch, PortMaster, the App Store">

- RetroArch 1.22 on one panel (the top by default), with its cores built for the RK3566's Cortex-A55: GBA (gpSP or
  mGBA), Game Boy (Gambatte), NES (FCEUmm), SNES (Snes9x), Mega Drive (PicoDrive), PlayStation (PCSX ReARMed), N64
  (Mupen64Plus-Next). The other panel is a game panel of touch tiles: save, load, menu, fast-forward, quit.
  Shaders out of the box (Settings > Other systems > Shader: LCD grid, CRT or sharp pixels).
- PortMaster with its ports, as on ROCKNIX.
- The App Store: a Store tile and an Apps shelf. Döda Kvarter, Bank & Trade and eReaderDS install and update from
  there.
- PICO-8 (fake-08, or the official PICO-8 when it is on the card).
- Search (Select): part of a name, a genre or a year, across every system. Recently played and
  Favorites shelves.
- Box art from libretro or [TwilightBoxart](https://boxart.kirovair.com/), and games on a second SD card on
  the RG DS Plus.
- File sharing over Wi-Fi: the card is `\\<address>\banana` (log in as `banana` / `linux`). Copy games in and they
  show up in the menu.

<a id="made-for-it"></a>
<img src="docs/img/readme/h-hardware.png" width="100%" alt="06: Made for it. Two panels, a lid, and no sound server">

- Two panels, one menu. Both 640x480 (RG DS) or 1024x768 (RG DS Plus) panels are driven directly; the menu, the
  boot animation and DS games use them as the DS did.
- 60 Hz panels. The RG DS's panels are retuned from 60.10 Hz to 60.0013 Hz in the device tree; on the RG DS Plus,
  `banana-panel-tune` reads the real pixel clock on the first boot and retunes both panels to 59.999 Hz.
- The clamshell. Closing the lid sleeps; opening it wakes the game where it was. Woken in a bag, it goes back to
  sleep. While asleep it checks the battery and shuts down cleanly before it could run flat.
- The LEDs, as on ROCKNIX: green, red at 20%, blinking red at 5%, amber while charging (asleep on the charger
  too).
- Sound with no sound server. The speakers, the RG DS Plus's AW88166 amplifiers or the headphones the moment
  they're plugged in, during a game too; the microphone always records from the rk817 at full level.

<a id="get-it"></a>
<img src="docs/img/readme/h-install.png" width="100%" alt="07: Get it. A card image, or an update over the one you have">

<p align="center">
  <a href="https://github.com/jesussaves-ai/banana-linux-flasher"><img src="docs/img/readme/flasher.png" width="100%" alt="BananaLinux Flasher by jesussaves-ai, the easy way onto a card: its window (choose your handheld, select the target drive, wipe and flash) next to a microSD card with the bunch on its label. 1, pick your handheld. 2, pick the microSD card. 3, Download and Flash. The newest release, SHA-256 checked, written straight to the card. Windows .exe, Linux, macOS. Get the flasher"></a>
</p>

The easy way: the [BananaLinux Flasher](https://github.com/jesussaves-ai/banana-linux-flasher), made by jesussaves-ai. Pick your handheld and
the card, and it downloads the newest BananaOS image, checks it against the release's SHA-256 and writes it to the
card, refusing your computer's own disks. On Windows, download `BananaLinuxFlasher.exe` from
[its releases](https://github.com/jesussaves-ai/banana-linux-flasher/releases/latest) and run it as administrator (it's an unsigned community
build, so SmartScreen may ask: More info > Run anyway). On Linux and macOS, run it from source with Python 3 and
Tkinter (`python3 flash_banana.py`).

Or by hand: download the image for your handheld from the
[0.4.2 release](https://github.com/JorreFog/BananaLinux/releases/tag/v0.4.2), and write it (don't unzip it) to a
microSD card with [balenaEtcher](https://etcher.balena.io/):

| Handheld | Image | Update package |
|---|---|---|
| **RG DS Plus** | [banana-rgds-plus.img.gz](https://github.com/JorreFog/BananaLinux/releases/download/v0.4.2/banana-rgds-plus.img.gz) | [banana-rgds-plus-0.4.2.update.tar](https://github.com/JorreFog/BananaLinux/releases/download/v0.4.2/banana-rgds-plus-0.4.2.update.tar) |
| **RG DS** | [banana-rgds.img.gz](https://github.com/JorreFog/BananaLinux/releases/download/v0.4.2/banana-rgds.img.gz) | [banana-rgds-0.4.2.update.tar](https://github.com/JorreFog/BananaLinux/releases/download/v0.4.2/banana-rgds-0.4.2.update.tar) |

On the first boot BananaOS makes the data partition from the rest of the card (a few seconds), then a setup asks
for Wi-Fi, the time zone, your DS's name and a RetroAchievements account (all skippable). Put games in `roms/nds`,
`roms/gba` and so on, from any computer or over file sharing (`banana/README.txt` on the card lists the folders).

An update (from 0.4.0 or 0.4.1): put the `.update.tar` in `banana/updates` on the card, then
Settings > System > Update, A on the package and A again. Games, saves and settings stay.

Moving from GammaOS or ROCKNIX? The [Save Mover](https://jorrefog.github.io/BananaLinux/save-mover/) backs up
your saves, converts them and puts them in the card's own folders, in your browser. A ROCKNIX `.dsv` next to a game
is taken as its save the first time.

<a id="status"></a>
<img src="docs/img/readme/h-status.png" width="100%" alt="08: Status. What works, and what is still rough">

Tested on an RG DS and an RG DS Plus for 0.4.2: 74 device tests passed on the Plus and 62 on the RG DS, and every
change was code-reviewed before the build. Both handhelds run the released update packages.

Still rough, or not done yet:

- In Chinese, lines added since the translation still show in English (Recently played, Search and a few rows).
- Golden Sun: Dark Dawn's opening flyover runs slow in BananaDS (the game itself holds 60).
- On the RG DS Plus, a DS shader with heavy 3D can warm the chip enough to drop a frame now and then in long
  sessions; a shader of None for that game (X in the library) holds 60.
- Charging while switched off on the RG DS Plus (#34), Bluetooth, and one card image for both handhelds.

What's next: [docs/ROADMAP.md](docs/ROADMAP.md). Every release's notes: [Releases](https://github.com/JorreFog/BananaLinux/releases).

<a id="build"></a>
<img src="docs/img/readme/h-build.png" width="100%" alt="09: Build. From source, on Linux">

On Linux (Debian/Ubuntu: `build-essential bc bison flex libssl-dev libelf-dev unzip rsync cpio python3-dev swig
libgnutls28-dev`). The released 0.4.2 is the tag `v0.4.2`:

```sh
make rgds          # output/rgds/images/banana-rgds.img.gz
make rgds-plus     # output/rgds-plus/images/banana-rgds-plus.img.gz
```

The first build fetches Buildroot and everything it builds, and takes an hour or more. The whole image boots in
QEMU with no handheld (`tools/qemu-boot.sh`, also the CI's `boot` job): the first boot, the menu on two virtual
panels, a game started from the library, a shutdown from the settings.

Working on the menu needs no handheld either:

```sh
make ui            # ui/banana-ui for this computer
make test          # renders the menu from a test library into ui/tests/out
```

| Path | What |
|---|---|
| `ui/` | `banana-ui`: the KMS host (`src/`), ROCKNIXDS Pixel's engine (`rnds/`), its assets, the headless tests |
| `system/bananad/` | `bananad` (sleep, lid, power button, LEDs, sound routing, volume and brightness, battery guard) and `banana-panel-tune` |
| `system/rootfs/` | init, first boot, `banana-perf`, sleep hooks, the game launchers, default settings |
| `buildroot/` | defconfigs, Banana's packages, the RG DS board (kernel patches, device trees, drivers, U-Boot, image layout) |
| `store/` | the App Store's catalogue |
| `tools/` | the menu engine's import, ROCKNIX's hardware sync, the pixel bunch, the logo, the boot animation, this page's pictures (`make-readme-art.py`) |
| `web/` | the website on GitHub Pages: the [Save Mover](https://jorrefog.github.io/BananaLinux/save-mover/) |
| `docs/` | [ARCHITECTURE.md](docs/ARCHITECTURE.md), [EMULATORS.md](docs/EMULATORS.md), [FIRST-BOOT.md](docs/FIRST-BOOT.md), [ROADMAP.md](docs/ROADMAP.md) |

<a id="credits"></a>
<img src="docs/img/readme/h-credits.png" width="100%" alt="10: Credits. The work Banana stands on">

- Simplified Chinese (简体中文): translated by Majirefy. The characters are drawn with
  [Fusion Pixel Font](https://github.com/TakWolf/fusion-pixel-font) (缝合像素字体) by TakWolf. To translate BananaOS
  into your language, ask for the translation document; `tools/lang-from-docx.py` turns a filled-in one into
  `ui/assets/lang/<language>.tsv`.
- jesussaves-ai: the [BananaLinux Flasher](https://github.com/jesussaves-ai/banana-linux-flasher), the card writer for Windows, Linux and macOS.
- casualducko: the zero-copy panel patch for LÖVE apps on both panels (eReaderDS).
- The menu is ROCKNIXDS Pixel, from [ROCKNIXDS](https://github.com/JorreFog/ROCKNIXDS), where BananaOS started.
  Banana's mark is the bunch of [BananaUI](https://github.com/JorreFog/helios), redrawn as pixel art.
- The RG DS's kernel support, device trees and drivers come from [ROCKNIX](https://rocknix.org). DS emulation is
  DraStic by Exophase (through [SuperDrastic](https://github.com/JorreFog/SuperDrastic)) and
  [melonDS](https://melonds.kuribo64.net/)'s core (BananaDS).
- [Retro Game Corps](https://www.youtube.com/@RetroGameCorps) and [forthenext](https://www.youtube.com/@forthenext)
  for showing BananaOS to a lot of people.

Details and licenses: [THIRD_PARTY.md](THIRD_PARTY.md). BananaOS contains no games, ROMs or BIOS files.

<p align="center"><a href="https://discord.gg/FxeE7f8r"><b>Questions, reports, setups: join the Discord</b></a></p>

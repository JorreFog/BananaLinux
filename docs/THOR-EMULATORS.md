# The AYN Thor's emulators

What each system on the AYN Thor's shelf runs, where its files go, and how far each piece has got. The list itself is
`buildroot/board/thor/systems.conf`; the packages are under `buildroot/package/` and are all turned on by
`BR2_PACKAGE_BANANA_THOR_EMULATORS` (`buildroot/package/thor-emulators/Config.in`).

**Status: on paper.** Every package below passes Buildroot's Kconfig and `show-info` with the Thor's defconfig, every
pinned commit exists upstream, and every patch taken from ROCKNIX applies to its pinned source (checked file by file).
None of them has been compiled for aarch64 yet, and none has run on a Thor.

## How a game starts

- **libretro cores** run in RetroArch through `run-retroarch <core> <game>`, as on the RG DS: KMS, GLES, no compositor.
  RetroArch here has no Vulkan driver, so the cores use their GLES renderers. `core.<system>` in
  `banana/settings.conf` picks another installed core for a system (the "other cores" in the table).
- **Standalone emulators** each have a launcher, `/usr/libexec/banana/run-<emulator>`. The launcher:
  - copies ROCKNIX's SM8550 settings from `/usr/share/<emulator>` to `/data/<emulator>` on the first run, and leaves
    them alone after that;
  - starts the emulator through `run-wayland`, a compositor of its own on the top panel. For the 3DS it uses
    `--two-screens`: Azahar's second window goes full screen on the bottom panel;
  - writes the log to `/data/banana/logs/<emulator>/last.log` (the previous one is `last.log.1`);
  - passes the menu's SIGTERM on to the emulator and exits with its status;
  - hands the emulator the RetroAchievements account logged in in BananaOS (below).
- **Folders.** An emulator with fixed folders of its own (PPSSPP's `PSP/SAVEDATA`, Dolphin's `GC` and `Wii`, Azahar's
  `sdmc` and `nand`) has them in BananaOS's `saves/` and `states/`. The card's data partition is exFAT, which has no
  symbolic links, so the launcher binds the folder there while the game runs (`usr/libexec/banana/data-link`; a link
  on a card formatted ext4).
- **RetroAchievements.** One login, in the first-time setup or Settings > Accounts, reaches every emulator that has
  achievements: RetroArch's cores as on the RG DS, and PPSSPP, Dolphin, Flycast and ARMSX2 through
  `usr/libexec/banana/ra-standalone`, which writes the account's name and login token into each one's own settings
  as its game starts and takes the token out again when it ends (file sharing shows those files). ROCKNIX leaves
  each of these to its own login screen.
- **Sound** is ALSA's `default` everywhere (SDL's ALSA driver, Dolphin's ALSA back end, Flycast's `alsa`, cubeb on ALSA).
- **Pads** are read through SDL. Each launcher sets `SDL_GAMECONTROLLERCONFIG_FILE` from
  `/data/banana/gamecontrollerdb.txt` (the card's own, when there is one) or the image's
  `/usr/share/banana/gamecontrollerdb.txt`, which has the Thor's own pad. Its line was worked out from the kernel
  driver's key list and the device tree's ids, and its id agrees with the one ROCKNIX recorded on a real handheld; the
  buttons' places were not tried on one.
- **Card layout:** games in `/data/roms/<id>`, BIOS files in `/data/bios`, saves in `/data/saves/<id>`, states in
  `/data/states/<id>`. Where an emulator keeps these elsewhere, the table says so.

"ROCKNIX" in the Status column means ROCKNIX's SM8550 package at its pin (`6cf94dba`): the same commit, build options
and patches. Each patch keeps its origin on its first line. ROCKNIX is GPL-2.0. "Adapted" lists what differs.

## System by system

| System (id, design id) | Emulator: default (other cores) | Kind | Package(s) | BIOS / user files | Status |
|---|---|---|---|---|---|
| NES (`nes`, nes) | BananaNES (Nestopia, FCEUmm) | core | libretro-banananes, libretro-nestopia, libretro-fceumm | none (FDS: `disksys.rom` in /data/bios) | BananaOS; Nestopia: ROCKNIX |
| Super Nintendo (`snes`, snes) | BananaSNES (Snes9x) | core | libretro-bananasnes, libretro-snes9x | none | BananaOS |
| Nintendo 64 (`n64`, n64) | Mupen64Plus-Next (ParaLLEl N64) | core | libretro-mupen64plus-next, libretro-parallel-n64 | none | BananaOS; ParaLLEl: ROCKNIX, adapted (its patch's `@DEVICE@` placeholder written as `thor`) |
| GameCube (`gamecube`, gc) | Dolphin, nogui player | standalone | dolphin | optional `/data/bios/GC/<USA\|EUR\|JAP>/IPL.bin`; memory cards in saves/gamecube | ROCKNIX (2606), adapted: no Qt front end, no X11, no PulseAudio |
| Wii (`wii`, wii) | Dolphin | standalone | dolphin | none; the Wii's NAND (its saves) in saves/wii | as GameCube |
| Wii U (wiiu) | — | — | — | — | **Left out**: see below |
| Switch (switch) | — | — | — | — | **Left out**: see below |
| Game Boy (`gb`, gb) | Gambatte | core | libretro-gambatte | none | BananaOS |
| Game Boy Color (`gbc`, gbc) | Gambatte | core | libretro-gambatte | none | BananaOS |
| Game Boy Advance (`gba`, gba) | BananaGBA (mGBA, gpSP) | core | libretro-bananagba, -mgba, -gpsp | none (`gba_bios.bin` optional) | BananaOS |
| Virtual Boy (`virtualboy`, vb) | Beetle VB | core | libretro-beetle-vb | none | ROCKNIX |
| Nintendo DS (`nds`, nds) | DraStic (SuperDrastic) or BananaDS (`run-nds`) | standalone | drastic, superdrastic, bananads | none | BananaOS. melonDS's own front end is not packaged: see below |
| Nintendo 3DS (`3ds`, 3ds) | Azahar (Qt 6), both panels | standalone | azahar | the 3DS's SD and NAND (installed titles, saves, system files the user dumped) in saves/3ds/sdmc and saves/3ds/nand | ROCKNIX (2126.2), adapted: Buildroot's Qt (`USE_SYSTEM_QT`), Wayland not XWayland |
| Pokémon mini (`pokemini`, pmini) | PokeMini | core | libretro-pokemini | optional `bios.min` | ROCKNIX |
| Game & Watch (`gameandwatch`, gw) | MAME (mame-gnw) | standalone | mame-gnw | the games' MAME zips and artwork | BananaOS (made for the RG DS's panels: not checked on the Thor's) |
| PlayStation (`psx`, psx) | SwanStation (PCSX-ReARMed, Beetle PSX) | core | libretro-swanstation, -pcsx-rearmed, -beetle-psx | `scph5501.bin` etc. in /data/bios | ROCKNIX; PCSX-ReARMed: BananaOS |
| PlayStation 2 (`ps2`, ps2) | ARMSX2, SDL 3 front end | standalone | armsx2 (+ sdl3, shaderc, plutovg, plutosvg, extra-cmake-modules) | a PS2 BIOS in `/data/bios/ps2`; memory cards in saves/ps2 | ROCKNIX's commit and patch, adapted: ARMSX2's SDL 3 front end instead of Qt, GCC instead of clang, the system's Turnip instead of ARMSX2's own |
| PlayStation 3 (ps3) | — | — | — | — | **Left out**: see below |
| PSP (`psp`, psp) | PPSSPP standalone (PPSSPP core) | standalone | ppsspp, libretro-ppsspp | none; saves in saves/psp, the memory stick in /data/roms/psp | ROCKNIX (v1.20.2; core v1.20.4), adapted: no cheat database or extra font, the states folder is states/psp |
| PS Vita (`psvita`, vita) | Vita3K | standalone | vita3k | the Vita firmware (`PSVUPDAT.PUP`, installed from Vita3K's menu); storage in /data/roms/psvita/vita3k | ROCKNIX |
| Xbox (`xbox`, xbox) | xemu | standalone | xemu (+ sdl3) | `/data/bios/xemu/bios/mcpx_1.0.bin`, `.../Complex_4627v1.03.bin`, `hdd/xbox_hdd.qcow2` (saves are on it) | ROCKNIX, adapted: SDL 3 from package/sdl3, no GTK, no hard-disk-image download |
| Xbox 360 (x360) | — | — | — | — | **Left out**: no aarch64 Linux emulator |
| Master System (`mastersystem`, sms) | Genesis Plus GX | core | libretro-genesis-plus-gx | none | ROCKNIX |
| Mega Drive (`megadrive`, md) | PicoDrive (Genesis Plus GX) | core | libretro-picodrive, -genesis-plus-gx | none | BananaOS; GPX: ROCKNIX |
| Mega-CD (`segacd`, segacd) | Genesis Plus GX | core | libretro-genesis-plus-gx | `bios_CD_U.bin`, `bios_CD_E.bin`, `bios_CD_J.bin` | ROCKNIX |
| 32X (`sega32x`, 32x) | PicoDrive | core | libretro-picodrive | none | BananaOS |
| Saturn (`saturn`, saturn) | YabaSanshiro (Beetle Saturn) | core | libretro-yabasanshiro, -beetle-saturn | `saturn_bios.bin` / `sega_101.bin`, `mpr-17933.bin` | ROCKNIX; YabaSanshiro adapted: two more `-Wno-` flags for GCC 14 |
| Dreamcast (`dreamcast`, dc) | Flycast standalone (Flycast core) | standalone | flycast, libretro-flycast | `dc_boot.bin`, `dc_flash.bin` in `/data/bios/dc` (optional: HLE BIOS) | ROCKNIX (v2.7), adapted: ALSA, no breakpad/libao |
| Game Gear (`gamegear`, gg) | Genesis Plus GX | core | libretro-genesis-plus-gx | none | ROCKNIX |
| Atari 2600 (`atari2600`, a2600) | Stella | core | libretro-stella | none | ROCKNIX |
| Atari 5200 (`atari5200`, a5200) | a5200 | core | libretro-a5200 | `5200.rom` (optional) | ROCKNIX |
| Atari 7800 (`atari7800`, a7800) | ProSystem | core | libretro-prosystem | `7800 BIOS (U).rom` (optional) | ROCKNIX |
| Atari Lynx (`atarilynx`, lynx) | Handy | core | libretro-handy | `lynxboot.img` | ROCKNIX's commit; adapted: `platform=unix` instead of its patched "emuelec" platform |
| Atari Jaguar (`atarijaguar`, jaguar) | Virtual Jaguar | core | libretro-virtualjaguar | none | ROCKNIX. Many games run with faults. BigPEmu (closed binary) not packaged |
| PC Engine (`pcengine`, pce) | Beetle PCE FAST | core | libretro-beetle-pce-fast | none | ROCKNIX |
| PC Engine CD (`pcenginecd`, pcecd) | Beetle PCE FAST | core | libretro-beetle-pce-fast | `syscard3.pce` | ROCKNIX |
| PC-FX (`pcfx`, pcfx) | Beetle PC-FX | core | libretro-beetle-pcfx | `pcfx.rom` | ROCKNIX (`platform=unix`: ROCKNIX's `armv` is the same in its Makefile) |
| Neo Geo (`neogeo`, neogeo) | FinalBurn Neo | core | libretro-fbneo | `neogeo.zip` (with the games or in /data/bios) | ROCKNIX |
| Neo Geo CD (`neocd`, neocd) | NeoCD | core | libretro-neocd | `/data/bios/neocd/` (e.g. `neocd_f.rom`, `000-lo.lo`) | ROCKNIX |
| Neo Geo Pocket Color (`ngpc`, ngpc) | Beetle NeoPop | core | libretro-beetle-ngp | none | ROCKNIX |
| WonderSwan Color (`wonderswancolor`, wsc) | Beetle Cygne | core | libretro-beetle-wswan | none | ROCKNIX |
| 3DO (`3do`, 3do) | Opera | core | libretro-opera | `panafz10.bin` (or another 3DO BIOS) | ROCKNIX |
| Arcade (`arcade`, arcade) | FinalBurn Neo (MAME 2003-Plus) | core | libretro-fbneo, -mame2003-plus | per romset | ROCKNIX |
| Naomi (`naomi`, part of arcade) | Flycast standalone | standalone | flycast | `naomi.zip` in /data/bios/dc | ROCKNIX |
| Atomiswave (`atomiswave`, part of arcade) | Flycast standalone | standalone | flycast | `awbios.zip` in /data/bios/dc | ROCKNIX |
| ColecoVision (`colecovision`, coleco) | Gearcoleco | core | libretro-gearcoleco | `colecovision.rom` | ROCKNIX |
| Intellivision (`intellivision`, intv) | FreeIntv | core | libretro-freeintv | `exec.bin`, `grom.bin` | ROCKNIX |
| Vectrex (`vectrex`, vectrex) | vecx (GLES) | core | libretro-vecx | none | ROCKNIX |
| Odyssey² (`odyssey2`, o2) | O2EM | core | libretro-o2em | `o2rom.bin` | ROCKNIX |
| CD-i (`cdi`, cdi) | SAME_CDI | core | libretro-same-cdi | `cdimono1.zip` in /data/bios/same_cdi/bios | ROCKNIX |
| MSX (`msx`, msx) | blueMSX | core | libretro-bluemsx | blueMSX's `Machines` and `Databases` folders in /data/bios | ROCKNIX |
| Commodore 64 (`c64`, c64) | VICE x64sc | core | libretro-vice | none | BananaOS |
| Amiga (`amiga`, amiga) | PUAE 2021 | core | libretro-puae2021 | Kickstart ROMs (AROS without them) | ROCKNIX (installed as `puae2021`, libretro's name) |
| ZX Spectrum (`zxspectrum`, zx) | Fuse | core | libretro-fuse | none | BananaOS |
| Amstrad CPC (`amstradcpc`, cpc) | CrocoDS (Caprice32) | core | libretro-crocods, -cap32 | none | ROCKNIX |
| Sharp X68000 (`x68000`, x68k) | PX68k | core | libretro-px68k | `/data/bios/keropi/iplrom.dat`, `cgrom.dat` | ROCKNIX |
| DOS (`dos`, dos) | DOSBox Pure | core | libretro-dosbox-pure | none | ROCKNIX |
| PICO-8 (`pico-8`, pico8) | the official PICO-8 if on the card, else fake-08 | core / user's binary | libretro-fake08 | none | BananaOS |
| ScummVM (`scummvm`, scummvm) | ScummVM standalone | standalone | scummvm | none (game files) | ROCKNIX (2026.1.0), adapted: no FluidSynth or sound font, so MIDI music plays as AdLib |
| PC games (pc) | — | — | — | — | not part of this work (Box64 + Wine) |

The shelf ends with BananaOS's own entries, as on the RG DS: Döda Kvarter, Bshare, Store, Activity, Apps, Ports, Files.

## Left out, and why

- **Wii U (Cemu).** ROCKNIX ships Cemu on the SM8550, but Cemu's Linux build needs:
  - the X11 client libraries;
  - GTK 3 with its X11 backend, which in Buildroot needs Mesa's GLX (desktop OpenGL through X);
  - wxWidgets 3.3, which Buildroot doesn't have.

  ROCKNIX has all of these through its XWayland stack. The Thor image is KMS and Wayland only, with GLES and EGL from
  Mesa. Adding Cemu means adding an X11 client stack and Mesa GLX to the image first.
- **PlayStation 3 (RPCS3).** ROCKNIX ships it, and on the SM8550 runs it with its OpenGL renderer. RPCS3 has no way to
  build without its OpenGL renderer, which links desktop OpenGL and GLEW. The Thor's Mesa gives only GLES and EGL (no
  libGL or libOpenGL), and Buildroot's GLEW wants GLX. Possible later: libglvnd in front of Mesa, plus GLEW built for EGL.
- **Switch.** ROCKNIX ships nothing for it on the SM8550, so there is no tested aarch64 Linux build to port. Eden
  and Citron (the design's names) are not packaged here. The yuzu lineage's legal history also makes packaging one the
  owner's decision, not a default.
- **Xbox 360.** No Xbox 360 emulator runs on aarch64 Linux (Xenia is x86-64 and Windows-first; the Android names in the
  design have no Linux build).
- **DuckStation.** Its licence since 2024 (CC BY-NC-ND 4.0) forbids redistributing modified builds and packaging it.
  ROCKNIX ships its unmodified AppImage. Here SwanStation, the libretro fork from DuckStation's GPL-3.0 days, takes its
  place.
- **melonDS's own front end.** ROCKNIX's default DS emulator on the SM8550. Its Linux build needs the X11 libraries
  (the same problem as Cemu), and the DS shelf already has DraStic and BananaDS, which draw both panels themselves.
- **Kronos (Saturn).** It needs desktop OpenGL 4.2 or newer; RetroArch here gives cores GLES only.
- **Beetle PSX HW.** The hardware renderers want desktop OpenGL 3.3 or Vulkan from RetroArch. The software build is in
  as `mednafen_psx`.
- **ScummVM's libretro core.** Its build clones its dependencies while it compiles. The standalone ScummVM is used.

## Known gaps

**Not built or run yet**

- Nothing above has been compiled for aarch64 or started on a Thor. The likely build problems are in the big C++
  emulators:
  - ARMSX2 with GCC (upstream builds it with clang);
  - Azahar against Qt 6.9 (it downloads Qt 6.10 when not told to use the system's);
  - Dolphin's and Vita3K's bundled dependencies cross-compiled.
- Each standalone launcher's options, folders and quit path come from ROCKNIX's start scripts and the emulators'
  sources, not from a run.

**Image changes needed (outside these packages)**

- **Vulkan through SDL 2** is on (`buildroot/external.mk`: Buildroot builds SDL 2 without it, and Flycast and Vita3K
  make their Vulkan surfaces through SDL).
- **The Thor's pad** is in the image's SDL database (above). Dolphin's profiles use the first joystick's evdev name
  as the SDL device name. ROCKNIX's face-button swap for pads whose SDL mapping puts A on the right is not carried
  over.

**Downloads**

- **xemu needs the network while it builds.** Its Meson subprojects (imgui, implot, genconfig, glslang, volk,
  SPIRV-Reflect, VulkanMemoryAllocator, tomlplusplus, xxhash, softfloat...) are wrap files that Meson downloads at
  configure time. ROCKNIX's build does the same. They aren't Buildroot downloads, so they have no hashes.
- **Large git downloads.** Dolphin, Azahar, Vita3K, Flycast and PPSSPP come with their submodules.
- **No `.hash` files.** None of the new packages has one, because no source was downloaded here (the sandbox can't fetch
  GitHub archives).

**Things the image doesn't include**

- **Not in the image:** PPSSPP's cheat database, xemu's free hard-disk image, and ARMSX2's PS2 patch archive (ROCKNIX
  downloads it at install). The xemu launcher says where its files go.
- **RetroArch has no Vulkan driver here.** ParaLLEl N64, Beetle PSX HW and SwanStation therefore use their GLES paths.
  Turning on Vulkan in RetroArch is a change to `package/retroarch`.

**Licences**

- Genesis Plus GX, FinalBurn Neo, MAME 2003-Plus and parts of Opera allow non-commercial distribution only, like the
  Snes9x already in the image.
- ARMSX2 is GPL-3.0+ and LGPL-3.0+, Azahar GPL-2.0+, Dolphin GPL-2.0+, xemu GPL-2.0+.
- Each package's `_LICENSE` says which licence applies.

**Settings screens**

- The Thor's Settings screen doesn't know the new cores or the standalone emulators' options. `core.<system>` in
  `banana/settings.conf` switches a system's core by hand.

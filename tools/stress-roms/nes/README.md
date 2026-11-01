# BananaNES stress ROM (stress-nes)

Own code (GPL-3.0-or-later), 6502 assembly for ca65 (cc65, user-local in ~/.local/opt/cc65) + procedural data from
`gen.py` (tiles, sprites, DPCM, song, map). MMC3 (mapper 4), 64 KiB PRG, 64 KiB CHR, 8 KiB battery PRG-RAM.
Build: `./build.sh [LEVEL]` -> `out/stress-nes.nes` (LEVEL 5 = realistic default; `-L1`..`-L10` scale entity counts and
sprite pressure) + `out/stress-nes.phases.tsv`.

NTSC, no input needed (attract loop), deterministic (fixed seed). One logic tick per vblank (NMI); the lag counter
(frames the logic overran) is recorded. 9 scenes x 1500 frames (~25 s each), loop ~225 s. The 6502 folds scroll, PRNG,
sprite counts, sprite-0 hit timing and entity positions into a 16-bit hash; at each scene end it writes
`hash, frame, lag, sprite-0 wait, sprites` to PRG-RAM `$6008 + scene*8` (signature `BSTRES` at `$6000`), saved as
the .srm on quit. Build-vs-build exactness = identical .srm + identical video/audio CRCs from `nesrun`.

| # | Scene | Models | Hardware pressure |
|---|---|---|---|
| 0 | title | title/menu screens | text, palette cycling, 6 sprites, all 5 APU channels |
| 1 | platformer | SMB3, Mega Man 3, Kirby | column streaming into nametables each 4-8 px, sprite-0 status bar (spin on $2002), 8x16 sprites (~20), CHR bank animation, HUD digits via VRAM buffer, palette cycling, PRG bank switch per column |
| 2 | parallax-irq | Kirby's Adventure, SMB3 ending, Crystalis, Ys-style water | MMC3 IRQ: 6 bands/frame (scroll + CHR bank per band); second half 28 IRQs/frame wavy water; 8x16 sprites |
| 3 | shooter | Gradius, R-Type, Life Force | up to 57-64 sprites, >8 per line, rotating OAM start (flicker), 2 px/frame scroll, explosion bursts (16 particles) + noise + DMC crash |
| 4 | town-rpg | Dragon Quest, Zelda II, Final Fantasy | slow scroll, 14 NPCs, text box typed 1 char / 4 frames, IRQ split to a fixed window, day/night palette |
| 5 | fighter-dmc | Double Dragon, digitised drums (Battletoads-style) | DMC sample DMA every beat, $4011 direct loads, $4015 polling, 8 PRG bank swaps/frame, sprite CHR banks as animation, screen shake |
| 7 | split-2006 | Kirby/SMB3-style split screens done with the four-write $2006/$2005 scroll trick | the same 6 IRQ bands as scene 2 but every band start rewrites the PPU address ($2006 x2 + $2005 x2): mid-frame address-register path of the PPU |
| 8 | vertical-scroll | 1943, Ikari Warriors, Kid Icarus | horizontal mirroring (MMC3 $A000), Y scroll through two name tables with a new 32-tile row streamed in every 4 frames, 12 dive-bombers (2x2) + 12 bullets (about 350 sprite-lines a frame) |
| 6 | loads | scene changes / fades in any game | rendering off, bulk nametable upload (2 KiB), code copied to PRG-RAM and executed from $6100, SRAM writes, 4-step palette fade |

Evidence per scene and calibration against fingerprints: see CALIBRATION.md.

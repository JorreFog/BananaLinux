# gbastress - a realistic GBA stress ROM

Own code (GPL-3.0 like BananaLinux), built with clang + ld.lld (`python3 build.py` -> `out/gbastress-{realistic,L1,L10}.gba`
+ `.phases.tsv`). No SDK, no BIOS code, no game assets. Deterministic, no input needed, loops forever (soak).

## Scenes (30 s = 1800 frames each, loop 3 min)
| # | Scene | What it does | Models (pattern) |
|---|---|---|---|
| 0 | racer | Mode 1 "Mode 7": per-line affine via HBlank DMA0 (table built each frame in IWRAM ARM code), 8 AI karts in 3 physics substeps with track-surface reads from VRAM and kart-kart collisions, 32 track items projected with BIOS Div, scaled (affine, double-size) kart sprites, PSG engine note | Mario Kart: Super Circuit, F-Zero: Maximum Velocity |
| 1 | shooter | Mode 0, 3 parallax layers, up to ~120 OBJ (ship, 30 enemies, 64 shots, 64 aimed enemy shots, explosions), shot/enemy/ship collisions, Y-sorted OAM (insertion sort), a big wave + OBJ tile upload + palette flash every 3 s | Gradius Galaxies, Iridion II, R-Type III |
| 2 | town | Mode 0, 4 layers: a 128x128-tile world streamed in rows/columns, 32 NPCs with map-collision wander AI, a VWF text box (glyphs shifted into 4bpp tiles each frame, a full redraw every 4 s = spike), window + alpha blend, 1 KB SRAM save + read-back every 10 s | Golden Sun, Pokémon FireRed/Emerald, Mother 3 |
| 3 | raster | Mode 0, an HBlank IRQ on every line (wave scroll, per-line sky colour, a blend switch at the water line), window, mosaic, 16 affine + ~80 plain sprites on screen (~97 OBJ: Aladdin keeps ~120), a player through 96 rings | Sonic Advance (water), Castlevania: Aria of Sorrow |
| 5 | tactics | Turn-based strategy map: the CPU **never halts** (waits for the vblank in a loop), an HBlank IRQ on every line, all 128 OBJ in use (units, movement-range markers, off-screen parked ones), a flood fill of the selected unit's range every 16 frames, damage forecasts with BIOS Div | Advance Wars (its fingerprint, measured with BANANAGBA_STATS on the owner's copy: busy 100 %, 229 IRQs/frame, 128 OBJ) |
| 4 | loader | Room-change spikes: BIOS LZ77UnCompVram of 16 KB tiles every 2 s, two code overlays copied into one IWRAM slot in turn every 30 frames (+ a write into the running code), 400 particles in the overlay, CpuFastSet palette cycling (256 colours a frame), 500 Thumb->ARM helper calls a frame, two 64x64 rotating sprites | Golden Sun (IWRAM/EWRAM overlays), most games' LZ77 loads |

Every frame: an 8-voice software mixer (IWRAM ARM) at 18157 Hz into Direct Sound A+B (timer 0, DMA1/2 FIFO, double
buffer swapped in the VBlank IRQ) driven by a 6-track sequencer + PCM effects, PSG effects, KEYINPUT polled,
VBlankIntrWait, OAM by DMA3 from a shadow, WAITCNT 0x4317. Load levels: L1 / realistic (L5) / L10 scale the entity
counts. GBA CPU use per frame (lines from vblank to the end of the work) is measured by the ROM itself.

## Self-reporting
- **Probe block** (companion spec section 7) at 0x0203FFE0: `BNPROBE1`, frame, scene, phase, running checksum.
- **Results** at 0x0203FF00 and in SRAM from offset 16: per phase frames, checksum, GBA CPU avg/max %, overruns.
  `results.py` prints them (from `lrhost --peek 0x0203FF00 224` output, or a `.srm` file).
- **Checksum** per phase: OAM shadow + mixer output + the Mode 7 table + game state, every frame. It must be the
  same on every correct core: mGBA and gpSP agree (realistic: f17a0186 a49c17ea 03479952 20767c25 66469542 2a124fbd).
- HUD: scene, level, a scene counter, frame, GBA CPU %.

## Running
- PC (correctness only, never speed): `~/banana-emu/common/lrhost/build/lrhost <core.so> out/gbastress-realistic.gba
  --frames 9600 --every 0 --peek 0x0203FF00 224 | python3 results.py`
- Handhelds: `stress-run.sh <dev> out/gbastress-realistic.gba 600 <tag>` (stress-test-roms skill) through the menu.

GBA CPU use on mGBA (realistic): racer 82 %, shooter 72 % (spikes 99 %), town 67 % (95 %), raster 74 %, loader 65 %.

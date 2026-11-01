# gbastress calibration

Status (2026-11-01 final check): **tactics** and **raster** are modelled on fingerprints of the owner's Advance Wars and
Aladdin; the whole ROM is **validated at the band level** (BananaGBA in BananaFront, same build, both handhelds):
real games 10.0-11.1 ms per frame (BananaFront's run time, 60 fps, 0 dupes) vs gbastress 9.6-11.8 per scene, the same
bottleneck (the CPU interpreter + memory: 60 % of the emulator thread), within the +-15 % rule. Racer, shooter, town and
loader have no matching owner's game (no Mode-7 racer / shmup / LZ77-heavy game was measured): unvalidated per scene.

| Scene | Evidence used to design it | Game check (scene vs game, device, commit, date) |
|---|---|---|
| racer | (3) Mode 1 + per-scanline affine by HBlank DMA is the documented Mode 7 technique (Tonc, "Mode 7" chapters); MKSC/F-Zero MV render the track this way with scaled OBJ karts | - |
| shooter | (3) GBA OBJ limit 128 / 1210 cycles a line (GBATEK); shmups keep 80-120 OBJ on screen | - |
| town | (3) Golden Sun / Mother 3 draw text with a VWF into tiles (hacking docs); RPG maps larger than the 256x256 BG are streamed by rows/columns | - |
| raster | (3) HBlank IRQ / HDMA raster effects (Sonic Advance water, Castlevania) | - |
| loader | (3) Golden Sun loads code overlays into IWRAM/EWRAM per area (GS hacking community); LZ77 via BIOS SWI 0x11/0x12 is in most games | - |
| audio | (3) m4a/Sappy (most GBA games) mixes Direct Sound voices in IWRAM ARM code at 13379 Hz default, up to 12 voices | - |

Fingerprint from the ROM itself (GBA CPU per frame, mGBA, realistic): 65-82 % average, spikes to 95-99 % (scene
loads, text box redraws, waves): heavy-game territory (80-100 % of the limit in the busiest scenes).

## Fingerprints of the owner's games (BananaGBA `BANANAGBA_STATS=1`, BananaFront, RG DS Plus, 2026-11-01)
| Game, spot | GBA CPU busy | IRQs/frame | DMA units/frame (starts) | OBJ in use | mode | used for |
|---|---|---|---|---|---|---|
| Advance Wars (USA Rev 1), title/menus after Start, A | **100 %** (no halt) | 229 (HBlank every line) | 112 (28) | 128 | 0 | scene 5 tactics added (busy-wait + HBlank IRQ + 128 OBJ) |
| Aladdin (RG DS), after Start | 66-68 % | 1 | 2260-2320 (191: HBlank DMA every line) | 118-120 on screen | 0 | raster: 40 -> 96 rings (~97 OBJ on screen) |
| Pokémon FireRed (RG DS, title/intro, no save) | 100 % | 2 | 624 (29) | 1 | 1 | (title only) |
| Pokémon Emerald, title -> continue | 28-37 % | 2-3 | 530-624 (28-29) | 128 | 0 | town (busy 65 %: heavier than Emerald's overworld) |
| gbastress racer / shooter / town / raster / loader / tactics | 84 / 65 / 70 / 79 / 67 / 100 % | 1 / 1 / 1 / 229 / 1 / 229 | 1048 (161) / 425 / 1240 / 408 / 409 / 380 | 29-92 / 128 | 1 / 0 | |
Note: "OBJ in use" counts entries not hidden by attr0 (games park unused ones off-screen without hiding them).

## Game check (BananaGBA final in BananaFront, defaults; game stats from devrun, 2026-11-01)
| Game (device, spot) | run ms/frame (avg) | CPU | fps / dupes | scene it backs |
|---|---|---|---|---|
| Advance Wars (Plus, menus) | 10.40 | 71 % | 60.00 / 0 | tactics (busy-wait, HBlank IRQ, 128 OBJ) |
| Pokémon Emerald (Plus, overworld after Continue) | 10.96-11.14 | 70-71 % | 60.0 / 0 | town (lighter: busy 28-37 % vs 65-70 %) |
| Pokémon FireRed (RG DS, title/intro) | 10.01 | 66 % | 59.9 / 0 | - |
| Aladdin (RG DS, after Start) | 10.59 | 77 % | 60.0 / 0 | raster (~120 OBJ, HBlank DMA) |
| gbastress realistic (RG DS) | 9.6-11.8 by scene | 75-94 % | 60 / 0 | |

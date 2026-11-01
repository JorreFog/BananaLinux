# CALIBRATION - stress-nes (BananaNES)

The owner has no NES games on either handheld, so scenes are calibrated against (1) workload fingerprints from the
instrumented core, (2) freely licensed homebrew games, (3) documented limits. A scene checked only against hardware
limits is **synthetic**. Nothing is validated against a commercial game (none available): every row says what it was checked against.

Fingerprint = per-frame counters from `-DBANANA_STATS` (`BANANANES_STATS=file`; tool `banana/tools/fingerprint.py`),
mean / p95 / max over 7200 frames (scripted pad for the homebrew; stress ROM: its 8 scenes = 12000-frame loop). Date 2026-11-01, commit see git log.

| ROM | insn | $2005 w | $2007 w | $2002 polls | APU w | mapper w | DMC bytes | IRQs | sprites on lines (sum/frame) | max/line | lines with overflow |
|---|---|---|---|---|---|---|---|---|---|---|---|
| stress-nes (MMC3) all 8 scenes | 9367 / 9674 / 11363 | 9 / 56 / 56 | 6 / 26 / 2296 | 993 / 2644 / 2662 | 5 / 11 / 18 | 38 / 203 / 203 | 7 / 24 / 30 | 4 / 27 / 27 | 271 / 506 / 512 | 7 / 8 / 8 | 5 / 16 / 32 |
| Nova the Squirrel 1.0.6a (MMC1) | 9705 / 9918 / 9962 | 1 / 2 / 6 | 139 / 741 / 3181 | 0 / 1 / 1 | 5 / 12 / 32 | 10 / 25 / 85 | 0 | 0 | 91 / 352 / 352 | 2 / 6 / 8 | 0 / 0 / 6 |
| Thwaite 0.04 (NROM) | 9695 / 9764 / 10081 | 2 / 2 / 3 | 5 / 64 / 3534 | 60 / 0 / 1815 | 7 / 10 / 13 | 0 | 0 | 0 | 228 / 352 / 412 | 5 / 8 / 8 | 1 / 8 / 24 |
| 240p Test Suite NES 0.23 (UxROM) | 9576 / 9925 / 11113 | 2 / 2 / 14 | 270 / 903 / 3095 | 4 / 0 / 173 | 3 / 4 / 4 | 1 / 2 / 30 | 0 | 0 | 368 / 736 / 736 | 5 / 8 / 8 | 0 / 0 / 0 |

Reading it:
- **insn ~9.6k/frame is the same for every game**: the 6502 runs every cycle (29 780 per frame) whatever the game does, so a
  NES core's cost is dominated by fixed work (CPU, PPU lines, APU) and barely depends on the scene: the Plus baseline (FCEUmm in
  RetroArch) used 46-47 % of one core in all 7 scenes. That is why optimisation numbers are quoted per frame, not per scene.
- The stress ROM covers what the three homebrew games do not: MMC3 IRQ splits (4 mean, 28 in the wavy-water half), DMC DMA
  (7 bytes mean), sprite overflow lines (flicker) and mapper writes. These are labelled **synthetic** with the documented limits
  (nesdev.org: 64 sprites, 8 per line, MMC3 scanline counter, DMC DMA steals 4 cycles per byte), because none of the three
  homebrew games uses MMC3/DMC.
- $2007 writes: the stress ROM's mean (6) is **below** the homebrew's (Nova 139, 240p 270 = full-screen redraws and text boxes),
  but its scene-load frames reach 2296 (the max row), so the spike is covered; the steady mean is a scrolling-game figure.
- Not covered by any ROM here: expansion audio, FDS, MMC5, sprites 0-hit-heavy HUD games beyond the platformer scene,
  PAL. The core's mapper code for those is untouched upstream code (kept, not optimised).

| Scene | Models | Evidence | Status |
|---|---|---|---|
| title | menus/title; busy-wait on $2002 (2.6k status reads/frame, as polling games do; Thwaite's boot loop reaches 1815) | fingerprint (low-load end), Nova/240p text screens | partly validated |
| platformer | SMB3, Mega Man, Kirby: column streaming + sprite-0 bar | $2002 spin 335/frame p95 = a status-bar game (SMB1: tens to hundreds of polls); $2007 ~ scroll uploads | synthetic + fingerprint-range |
| parallax-irq | MMC3 raster effects | IRQ counts exceed anything homebrew here does; documented MMC3 usage (nesdev "MMC3"): status bars, split scrolls | synthetic (upper bound) |
| shooter | Gradius/R-Type sprite pressure | 57 sprites, overflow lines; Thwaite's sprite counts are lower (max 8/line, 412/frame) | synthetic (documented 64/8 limits) |
| town-rpg | text boxes + IRQ window | Nova text scenes ($2007 741 p95) are heavier than this scene | partly validated (lighter) |
| fighter-dmc | digitised drums | DMC bytes/frame 24 p95 (a looping sample = 8-16 bytes/frame at rate $0F...$0C), $4011 direct loads | synthetic |
| split-2006 | split screens via $2006 | MMC3 IRQ + $2006 trick = what SMB3/Kirby-class games do for status bars and split scroll; none of the homebrew uses it | synthetic |
| vertical-scroll | 1943 / Ikari / Kid Icarus | not covered by the homebrew set (none scrolls vertically); fingerprint: ~350 sprite-lines, 8 PPU data writes a frame, no $2002 polling | synthetic |
| loads | scene transitions | $2007 up to 2296 in a frame vs Nova 3181 / Thwaite 3534 / 240p 3095 | validated (same order) |

Exactness gate: `banana/tools/check.sh core.so` (video + audio + save-RAM + state CRC on the three stress levels and the
three homebrew ROMs, against the unmodified upstream core); on the handhelds `banana/tools/devcheck.sh`.
Homebrew sources / licences: see banana/roms/README (not in git): Nova the Squirrel (GPL-3.0, NovaSquirrel), Thwaite
(GPL-3.0, Damian Yerrick), 240p Test Suite (GPL-2.0, Damian Yerrick & others).

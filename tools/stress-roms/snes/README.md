# snesstress - SNES stress ROM for BananaOS (BananaSNES, RetroArch's Snes9x)

LoROM 512 KiB + 8 KiB SRAM, NTSC. Own 65816 (ca65) and SPC700 (mini assembler in `gen_spc.py`) code, procedurally drawn
art, own synthesized samples. No third-party code or assets in the ROM. Build: `python3 build.py [--level N] [--out DIR]
[--phase-frames N] [--start-phase N]` (needs cc65: `ca65`/`ld65`, user-local in `~/.local/opt/src/cc65/bin`, override
with `CC65=`). Output: `out/snesstress.sfc` and `out/snesstress.phases.tsv` (start second + name, for stress-run.sh).

Four 30 s phases (1800 frames each, the loop is 2 min; `--phase-frames` shortens them for tests). Every phase restarts
its scene deterministically (fixed seed), fades in/out over 15 frames and loads its graphics in forced blank (a spike).

| Phase | Models | What it does (hardware) | Evidence |
|---|---|---|---|
| racer | Super Mario Kart, F-Zero, Pilotwings: mode 7 floor | mode 7 matrix + centre + scroll rewritten per 4 lines by 4 HDMA channels (16 B per entry), BGMODE HDMA switching mode 1 sky / mode 7 floor at the horizon, 5 rival karts projected with the hardware divider/multiplier, dust/HUD/label sprites, per-frame VRAM DMA (kart frame, map row, water tile) | guest CPU 90 % of the frame (V-counter fingerprint), see CALIBRATION.md |
| action | Contra III, Gunstar Heroes, Super Turrican: run-and-gun | BG1 column streaming (64x32 ring, vertical DMA), BG2 parallax bands + 40-line wavy water via HDMA, animated backdrop gradient via COLDATA HDMA (colour math on the backdrop), BG3 HUD, ~60-100 sprites (24 enemies with 3 AI kinds, 16 bullets x 24 enemies AABB, 8 enemy shots, 40 debris, 16 embers, explosions), palette cycling (512 B CGRAM DMA), score digits via hardware divide | 80 % |
| town | Chrono Trigger, Zelda ALTTP, Final Fantasy overworld | two 64x64 BG layers (ground + houses/trees) streamed from a 128x64 world, 4 DMAs per new column, both scrolling with a vertical sway, inverted window vignette, V-IRQ mid-frame MOSAIC write (blur under the text box), typed text box on BG3 (one tile per 4 frames), 16 NPCs with random walks + shadows, 8 birds, palette-cycled water, a 160-op/frame script VM (jump table), one streamed voice | 73 % |
| audio | menu/title with music | all 8 DSP voices: 6 looping instruments with vibrato/tremolo + ADSR (a sound driver on the SPC700: 125 Hz timer, sequencer, port protocol), 2 streamed voices fed through the ports (2 bytes per exchange, ring buffers in SPC RAM), echo + FIR on all voices, noise on voice 7, pitch modulation voice 3, 8 VU bars (~50 sprites) | DSP voice/echo state is what costs |

Music plays in every phase (the SPC700 driver's per-scene voice mask: racer 6 voices, action 7, town 5, audio 8; echo, noise and
pitch-modulation masks per scene in the driver's tables). The streamed voices run in town (1) and audio (2) only: every
port exchange costs the main CPU ~500 cycles, which the racer and action loads do not leave room for.

## What it reports (all deterministic, nothing needs input)
- **SRAM** (`/data/saves/...` `.srm`, 8 KiB): "SSTR", loop count, then 16 bytes per phase at $10+16*phase:
  frames done, state checksum (rotating hash of positions/scores, proves the guest ran identically), load sum
  (sum over frames of the scanlines past the NMI line when the logic finished: divide by frames = guest CPU load in
  lines of 262), load max, lag frames (NMI hit while the logic was still running: must be 0), loop, phase.
- **Probe block** `BNPROBE1` at $7F:FFE0 (universal companion test, ~/banana-emu/common/COMPANION-SPEC.md).
- On the PC `run_pc.sh` + `~/banana-emu/bin/lrrun` (libretro host, correctness only): per-frame video+audio CRC, SRAM and
  WRAM dumps, PPM frames; `peek.py` reads variables from the WRAM dump by name.
- Per-phase **frame hashes** (lrrun) are the exactness gate for emulator changes: the core's output must be bit-identical.

Levels (`--level 1..10`, default 5): racer lines per HDMA entry (8/4/2) and rival count, more later.

Licence: GPL-3.0 like the rest of BananaLinux. Toolchain: cc65 (zlib licence), user-local.

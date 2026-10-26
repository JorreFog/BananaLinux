# BananaOS 0.5.0 beta: BananaDS performance data

BananaDS in 0.5.0 beta against BananaDS in 0.4.2, measured on an **Anbernic RG DS Plus** on 2026-10-26.

| | Build | BananaDS commit |
|---|---|---|
| Before | BananaOS 0.4.2 | `5390f7c` |
| After | BananaOS 0.5.0-beta.2 | `b4c6c67` (`release/0.5.0-bd` in SuperDrastic) |

## Speed: fast-forward throughput at 2x

How fast BananaDS can emulate when it isn't held to 60 fps: fast-forward at Max, the CPU held at 1992 MHz, 30 s
per run, measured on the handheld's own screens through the normal launcher. **Higher is better**. Both builds at
2x internal resolution with the same settings.

| Game | Starting point | 0.4.2 (emulated fps) | 0.5.0 (emulated fps) | Change |
|---|---|---:|---:|---:|
| Nintendogs: Best Friends | title | 123.9 | 154.0 | **+24.3%** |
| Pokémon HeartGold: Origin (hack) | save state, first grass area | 108.8 | 127.4 | **+17.1%** |
| Pokémon Black Version 2 | save state | 81.3 | 93.6 | **+15.1%** |
| Pokémon Platinum | intro | 199.1 | 228.5 | **+14.8%** |
| Mario Kart DS | title / demo race | 101.6 | 115.0 | **+13.2%** |
| Golden Sun: Dark Dawn | save state | 93.8 | 104.5 | **+11.4%** |
| Pokémon HeartGold | intro | 207.1 | 229.9 | **+11.0%** |
| Metroid Prime Hunters | title | 118.9 | 123.8 | **+4.1%** |
| Dragon Quest Monsters: Joker | title | 101.3 | 105.3 | **+3.9%** |
| **Average (geometric mean)** | | | | **+12.6%** |

## Efficiency: a full-speed game at normal settings

Pokémon HeartGold: Origin, first grass area, 60 s, the handheld's normal settings (2x, CPU governor on). Two runs
of each build, alternated.

| | 0.4.2 | 0.5.0 |
|---|---|---|
| Frame rate | 60.15 fps | 60.17 fps |
| Frames lost | 0 to 1 | 0 |
| Audio dropouts | 0 | 0 |
| CPU used, all BananaDS threads (100% = one core) | ~126% | **~94% (−25%)** |
| SoC temperature at the end | 57 °C | **51-52 °C** |

Both hold full speed here. 0.5.0 needs about a quarter less CPU to do it and runs about 5 °C cooler, which leaves
more headroom in heavier scenes and is easier on the battery.

## What changed in BananaDS

- **The CPU emulator (JIT):** the most used DS registers stay in the handheld's own CPU registers across compiled
  code blocks, and leaving a compiled block got cheaper. Interrupts with nothing to do no longer drop back to the main loop.
  Loops keep their registers in CPU registers (a bug that once froze HeartGold is fixed). Block lookups are
  prefetched, and memory block transfers and the divider are cheaper.
- **3D:** one render thread wakes per frame instead of three, polygons that are off screen are dropped before
  clipping, and unchanged parts of a still 3D frame are reused.
- **2D:** one job per line instead of two, and unchanged bottom-screen lines are reused.
- **Display:** the LCD-grid shader runs as a faster built-in copy, so the GPU clock is halved (800 to 400 MHz). The
  emulator thread has a CPU core to itself, and the idle microphone thread no longer runs.

**Exactness:** none of these changes alter the emulation. The emulated sound of HeartGold, Golden Sun, Black 2 and
Metroid Prime Hunters is bit-for-bit identical to the previous build, and nine games pass the start, picture, sound
and quit checks.

## How it was measured

- **Same conditions for both builds:** the same handheld, the same card and settings, alternating builds, and each
  run started only once the SoC was below 50 °C.
- **Starting points:** games without a save state started from their title or intro, which is lighter than gameplay.
- **Noise:** one pair per game, and run-to-run noise is about 2-3%, so the smallest gains are within noise.
- **Not covered:** the RG DS (non-Plus) was not measured.
- **Experimental 3x/4x:** not in these tables (0.4.2 has no 3x or 4x).

# Performance baseline

A host-side measurement of `banana-ui`, so a change can be told from a regression. It is **not** the handheld's
numbers: it runs on a PC, headless, with no panel and no GPU. What it is good for is (a) the *ratio* between the
640×480 and 1024×768 canvases and (b) catching a change that makes the menu slower. The handheld's own costs are
`docs/ARCHITECTURE.md`'s "What it costs", measured inside the VM (`tools/qemu-boot.sh`, `resources-*.txt`).

## Running it

    make bench                 # 600 games, 3 runs, both canvas sizes
    make bench BENCH_GAMES=200 # or: ui/tests/bench.sh 200 1

`ui/tests/bench.sh` builds a synthetic library (sparse ROMs, no art), then runs banana-ui headless with the `boot`
and `bench` script commands. The middle run is reported (the first is cold). Every line is from banana-ui's log:

- **scan** — `Library::scan()`: read every shelf's folder, its `gamelist.xml`, Banana's play stats and favourites.
- **menu** — the menu's constructor (the theme's build) plus the first settle: the layout of the home shelf and
  the pictures' decode.
- **first-frame** — the first `Render()`, the pixels the panel sees first: the rasterizing of boxes and glyphs.
- **bench** — N frames 16.7 ms apart; `average`/`worst` are the main thread's time per frame, the drawing alone,
  and the picture worker's; then how many of the screens were unchanged and how many pixels were drawn again.

## Baseline (2026-10, this repo's `main`)

A Ryzen host, `-O2`, one thread, 600 games across the shelves. Idle = the shelf at rest; scrolling = a `right`
press every 10 frames.

| Canvas | scan | menu | first-frame | idle frame | scrolling frame | pixels redrawn (scroll) |
|---|---|---|---|---|---|---|
| 640×480 | ~1.6 ms | ~3.3 ms | ~9.5 ms | ~0.07 ms | ~0.97 ms avg / 1.9 ms worst | ~41% |
| 1024×768 | ~1.8 ms | ~10 ms | ~45 ms | ~0.07 ms | ~2.8 ms avg / 6.5 ms worst | ~49% |

What the numbers say:

- **The scan is cheap, even at 600 games** (~2 ms): it is directory reads plus two small text files per shelf, not
  a decode. It scales with the *number of files*, not their size. It is not a boot bottleneck on this library size;
  a card with tens of thousands of ROMs (or a large `gamelist.xml`) is the case to re-measure.
- **The first frame is the expensive part** (~10 ms at 640, ~45 ms at 768): the glyph and box rasterization, and it
  scales with the canvas area (2.56× more pixels ≈ 4.7× the time, so it is not linear — the RndsRaster glyph cache
  is per-size and rebuilt at the larger scale). This is why the boot animation is played *while* the menu is built
  and the first frame is drawn under the iris, not on the panel: `irisToMenu()` in `src/main.cpp`.
- **Idle frames cost nothing** (~0.07 ms, 598 of 600 screens unchanged): the draw-list diff holds, as
  `docs/ARCHITECTURE.md` claims (the "well under a millisecond on a PC").
- **Scrolling is 1–3 ms**: ~41–49% of pixels redrawn — the list tiles and the shelf that moved, not the whole
  screen. This is the number to watch for I-1 (SIMD `soft.cpp`) and I-4 (`kms.cpp` rotation).

## Re-baselining

Re-run `make bench` after a change that touches `soft.cpp`, `kms.cpp` or `library.cpp`, and update this table when
the numbers move on purpose (a faster path) rather than by accident (a regression). CI does not assert these
numbers (a shared runner is too noisy); they are for a person to read.

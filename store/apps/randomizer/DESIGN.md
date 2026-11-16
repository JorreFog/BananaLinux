# Randomizer — the design it follows

**Status: implemented.** The app is `banana-randomizer.cpp` plus the baked font
`randomizer-font.h`, built to the static aarch64 `banana-randomizer` in this folder. README.md
has the build, the install, the data and the controls. This document is the visual system it
follows, the layout of each screen, and why the choices were made — useful when changing it.

The design follows the menu's own language: the ROCKNIXDS Pixel look as drawn by
`ui/rnds/RndsUI.cpp` and `ui/rnds/RndsRaster.cpp`, with the palette from
`ui/src/settings_pal.cpp`, the accent table from `ui/rnds/RndsUI.cpp`, and the component
recipes Activity uses in `store/apps/activity/DESIGN.md`.

**Previews:** `mockups/dark-*`, `light-*`, `plus-*` and `icon-*` are rendered by the app
itself (`--smoke`, no device needed), from the made-up card in `tools/make-testdata.py`.

---

## 1. The idea on screen

The app exists to answer one question: **what do I play now?** Everything on the top screen
serves the answer; everything on the bottom screen serves acting on it.

- The **game is the hero**: its cover (or its DS icon, per the menu's setting) at the largest
  size the sheet allows, not a list with a thumbnail.
- The **title and the numbers** are context, on the right, in the menu's game-page grammar:
  system plate, title, genre/year, then plays, time and last played.
- The **three choices** are the only controls: three identical quiet plates, one each for start,
  another pick and closing. They are also touch rows; the key cap and label say what each does.
- **Nothing else is on screen**: no filters, no history, no settings. The library is the app's
  whole world.

### The two screens

```
 TOP (display)                                  BOTTOM (touch + display)
 status: wifi · 82% · [battery] · [RANDOMIZER]  11/16  10:32
 ┌ sheet ─────────────────────────────────────┐  RANDOMIZER            6 GAMES · 3 SYSTEMS
 │ YOUR LIBRARY CHOSE                 1 OF 6  │  ┌ Peel Quest ──────────────────────────┐
 │                                             │  │ NINTENDO DS · 12 PLAYS               │
 │  ┌ cover ──────┐   [NINTENDO DS]            │  └──────────────────▼───────────────────┘
 │  │             │    Peel Quest               │  ┌ A  START GAME          PLAY IT NOW ┐
 │  │   box art   │    PUZZLE · 2024            │  └──────────────────────────────────────┘
 │  │             │    ───────────────────      │  ┌ X  RANDOMIZE AGAIN     PICK ANOTHER┐
 │  │             │    PLAYS            12      │  └──────────────────────────────────────┘
 │  └─────────────┘    PLAY TIME    4h 18m     │  ┌ B  CLOSE          BACK TO THE MENU┐
 │     BOX ART         LAST PLAYED  SATURDAY    │  └──────────────────────────────────────┘
 └─────────────────────────────────────────────┘  6 GAMES · 3 SYSTEMS · BOX ART
```

The geometry is the menu's: 640×480 CSS units, status row at y 20–45, sheet at (18,60) 604×406,
scaled by the panel's width (×1.6 on the RG DS Plus). The bottom screen has no status row; its
top line is the app's name and the pool size.

---

## 2. The system's tokens

### Palette (dark — the default; light in parentheses)

The values are the menu's (`ui/src/settings_pal.cpp`), the same table Activity uses.

| Token | Dark | Light | Used for |
|---|---|---|---|
| `ink` | `#f1ede2` | `#2a2620` | body text, values |
| `muted` | `#a39d8e` | `#6b6252` | labels, secondary lines |
| `line` | `#8a8374` | `#a69d8c` | dim borders |
| `edge` | `#e8e2d3` | `#3a342c` | bright 3 px borders, cover frame |
| `navy` | `#fff6dc` | `#2a2620` | the title, the big empty-state line |
| `red` | `#ff3b4a` | `#e23b4a` | the error toast |
| `screenBg` | `#17161c` | `#f3efe6` | the screen |
| `screenFrame` / `screenBand` | `#0b0a0d` / `#2e2b33` | `#d4ccbd` / `#fdfcf9` | 4 px frame, 8 px top band |
| sheet | `mix(accent, 34%, #15141a)` | `mix(accent, 20%, #a39d8e)` | the hero panel |
| `panelBand` / `panelBody` / `panelLow` | `#3a3540` / `#17161c` / `#0b0a0d` | `#ffffff` / `#f6f2ea` / `#d8d0c0` | the three option plates, the empty die frame |
| `bubbleBand` / `bubbleBody` / `bubbleLow` | `#38333b` / `#191820` / `#0f0e13` | `#ffffff` / `#faf7f0` / `#ddd5c6` | the title bubble |
| `rule` | `#45403d` | `#d6cfc0` | the hairline under the meta line |
| `brandBg` | `#262219` | `#262219` | the "RANDOMIZER" plate |
| `kbdBg` / `kbdInk` | `#f1ede2` / `#17161c` | `#3a342c` / `#f2f6fa` | the A/X/B key caps |
| `favBand` / `favBody` | `#5a2430` / `#2a1016` | `#f2b8bb` / `#f7d7d8` | the toast |

**Accent** (`settings.conf`'s `accent`; the default is yellow):

| name | base | light | dark | band | low |
|---|---|---|---|---|---|
| yellow | `#f0b429` | `#ffe68a` | `#8a5a00` | `#ffcf5c` | `#c98f12` |
| blue | `#4a9fe0` | `#b7e1ff` | `#0f3f66` | `#7cc0f2` | `#2f7fc0` |
| red | `#e8574d` | `#ffb8b0` | `#7a1a14` | `#f58a80` | `#c23b32` |
| green | `#56bf6b` | `#bff0c8` | `#1d5a2b` | `#86d896` | `#3d9a52` |
| purple | `#a07ae6` | `#ddd0ff` | `#43266e` | `#bfa4f2` | `#7f58c4` |
| pink | `#ec6fa8` | `#ffc6e0` | `#7a1f4a` | `#f59cc4` | `#c9508a` |

Text on the **system plate** is `#17161c` or white by contrast (`inkOnColour`): dark on yellow,
green, pink and light blue; white on the darker accents.

The **system plate** uses the system's own accent from the theme's `systems.cfg` — the one place
outside the app's accent, exactly as the shelf's tiles do. Its text uses the same contrast rule.

### Type scale (Pixelify Sans; whole-pixel sizes at 1×)

| Role | Font | Size | Tracking | Case |
|---|---|---|---|---|
| App name / brand plate | Medium | 16 | 1.3–1.4 | UPPER |
| Screen title (empty state) | Medium | 40 | 0.5 | UPPER |
| Game title, top | Medium | 34 | 0.3 | as written |
| Game title, bottom bubble | Medium | 26 | 0.52 | as written |
| Option title | Medium | 25 | 0.2 | UPPER |
| Stat value | Medium | 22 | 0 | as written |
| Eyebrow / section label | Regular | 13 | 1.4 | UPPER |
| Meta line | Regular | 13 | 1.2 | UPPER |
| Option sublabel | Regular | 12 | 1.2 | UPPER |
| Cover caption / footer | Regular | 11 | 1.2–1.4 | UPPER |
| Key cap | Medium | 22 | 0 | — |

Nothing below 11 px. The title wraps to three lines and ellipsises the third; the bottom title
ellipsises at 548 px; the system plate ellipsises to fit 286 px.

### Shape and depth recipes (CSS values, ×1.6 on the Plus)

| Part | Recipe |
|---|---|
| Screen | radius 2; 4 px `screenFrame`; 8 px `screenBand` at 50%; 4×4 dither tile (white 14% at (0,0), 7% at (2,2); dark 8%/4% in light) |
| Sheet | 604×406 at (18,60); r18; border 3 `edge`; dither; inset `(0,6)` white 10%, inset `(0,−8)` black 35%; outer `(4,4)` black 45% |
| Cover frame | the picture's fitted size + 6; r6; border 3 `edge`; shadow `(5,5)` black 47%; the icon's plate is `mix(accent, 18%, panelLow)` |
| System plate | r8; 2 px `accent.light` border; band 0–6 `accent.band`; shadow `(2,2)` 80% |
| Title bubble | 592×74 at (24,52); r12; border 3 `line`; band 0–10 `bubbleBand`; inset `(0,−6)` `bubbleLow`; pointer 16 px |
| Option plate | 592×64 at (24,154/230/306); r14; border 3 `line`; band 0–7 `panelBand`; inset `(0,−4)`; shadow `(3,3)` 82% — the same recipe for all three |
| Key cap | 40×40; r8; `kbdBg`; 2 px border; label 22 Medium centred |
| Toast | 592×46 at (24,398); r12; `favBody`/`favBand`; 3 px `red` border |

Motion: none beyond the frame. The pick is drawn as soon as the first frame is ready, and X
redraws immediately; the menu's quick fades would delay the answer, which is the one thing this
app is for.

---

## 3. The screens

### 3.1 Top — the pick

- **Eyebrow** `YOUR LIBRARY CHOSE`, and `1 OF N` right-aligned. The count is the honest one:
  every game is equally likely; `1 OF N` answers "how big is my library" without a second view.
- **The cover**, 250×300 area at (42,108), fitted by aspect:
  - a DS box (about 0.72 wide/high) becomes 216×300, a 2:3 box 200×300, a square 250×250,
    centred in the area;
  - a box is drawn smoothly (it is a photograph of a box, not pixel art);
  - a box's frame is 3 px of `edge`, the picture's own size, with the hard shadow.
- **The DS game icon** is different material: 32×32 pixel art, so it is blown up on whole source
  pixels — 7× (224) at 640 and 11× (352 device = 220 CSS) on the Plus — never bilinear. The
  menu's cached icon for a zipped game is already enlarged for the panel (214 CSS px); it is
  drawn 1:1 at 214. The frame's plate is `mix(accent, 18%, panelLow)`, so the transparent corners
  of the icon sit on a tinted card rather than a black hole; a 3 px accent line closes the
  bottom edge. This is the one place the app says "this is a cartridge icon", not a picture.
- **The caption** under the cover names the material: `BOX ART`, `DS CARTRIDGES: GAME ICON`,
  `DS GAME ICON`, `SYSTEM ICON` or `NO ART`.
- **Right column** from x 310:
  - the system plate at y 106 (the system's accent, its name in caps);
  - the title at y 144, 34 px, three lines at 40 px line height;
  - the meta line at y 278: genre · year (or the system's name), muted caps;
  - a hairline at y 298;
  - three label/value rows at y 310/348/386: `PLAYS`, `PLAY TIME`, `LAST PLAYED` (labels 11 px
    muted; values 22 px ink, right-aligned at 598). `NEVER` and an em dash are quiet answers, not
    zeros.
- **No progress meter, no row list, no carousel**: the app is one answer, and a second game on
  screen would dilute it. X is the only way to see another, and it is on the other screen.

### 3.2 Bottom — the three choices

```
RANDOMIZER                                       6 GAMES · 3 SYSTEMS
┌ Peel Quest ───────────────────────────────────────────────────────┐
│ NINTENDO DS · 12 PLAYS                                            │
└──────────────────────────────▼────────────────────────────────────┘
┌ A  START GAME                                        PLAY IT NOW ┐
└───────────────────────────────────────────────────────────────────┘
┌ X  RANDOMIZE AGAIN                                   PICK ANOTHER ┐
└───────────────────────────────────────────────────────────────────┘
┌ B  CLOSE                                       BACK TO THE MENU ┐
└───────────────────────────────────────────────────────────────────┘
                     6 GAMES · 3 SYSTEMS · BOX ART
```

- The **bubble** is the menu's selected-item bubble: the game's title at 26 px, its system and
  plays underneath. It is the only thing on the bottom screen that names the game, so it is the
  largest text there.
- All three choices are the **same quiet panel plate**: the same fill, top band, `line` border
  and shadow. START is first and is identified by its `A` key cap and 25 px label, not by a
  different colour; the other two carry `X` and `B`. The sublabels (`PLAY IT NOW`,
  `PICK ANOTHER`, `BACK TO THE MENU`) are the menu's voice: a short answer, not a sentence.
- The **footer** repeats the pool and the material (`6 GAMES · 3 SYSTEMS · BOX ART`), so the
  screen says what the pick was drawn from and which DS cartridge setting produced the picture.
- **Touch** anywhere on a row does what its key does. The top screen is display only.

### 3.3 Empty and error states

```
                 ┌ die ────────┐
                 │   32×32     │          (the app's own shelf icon, 5×; 8× on the Plus)
                 └─────────────┘
                    NO GAMES YET
   THE RANDOMIZER READS THE SAME FOLDERS AS THE MENU.
 COPY .NDS, .GBA, .SFC FILES TO ROMS/<SYSTEM> AND OPEN IT AGAIN.
```

- **No games**: the top sheet centres the app's die icon and the two-line hint; the bottom has
  the bubble `NOTHING TO PICK` and only **CLOSE**. No START/REROLL rows that would do nothing.
- **Start failed** (the file is gone, or `/run/banana/launch` can't be written): a red toast on
  the bottom screen for 2.6 s, `THAT GAME'S FILE IS GONE` / `COULDN'T START THAT GAME`; the app
  stays open so another game can be picked. The toast replaces the footer, not the options.
- **Only one game**: X shows `THAT'S THE ONLY GAME IN YOUR LIBRARY` rather than silently
  drawing the same game again; START still works.

---

## 4. What is deliberately not here

| Not | Why |
|---|---|
| A list, a carousel or a "recently played" row | The app is one answer; a list would be the menu with extra steps. |
| Filters (system, genre, players) | The request is "a random game from my library", not "a random GBA game"; the menu has search and system shelves. |
| A rolling/slot animation | The pick must be on screen the moment the app opens; a slot delays the answer and costs frames. |
| Sounds | The app has no ALSA dependency; the menu's cue would need the audio device the menu has just released. |
| A history / "don't repeat" set | Not requested; uniform random is honest. X only avoids the immediate repeat. |
| Translating the strings | Activity is English-only too; the table is a later, separate step. |
| A user theme's own colours and per-system icons | The menu's default `rnds/systems.cfg` supplies the systems' accents and icons; reading a user theme's `themeFiles.icons` is a separate pass. |
| A Store tile / catalog entry | It ships in the image like Activity; the shelf tile is `systems.conf`'s `[randomizer]`. |

The one thing that *is* here and could be considered scope creep is the **light palette**. It is
the menu's own second palette and costs one function; a Randomizer that stayed dark on a light
handheld would read as a port, not part of the system.

---

## 5. How it is built

1. **Text is the real menu font, baked.** `randomizer-font.h` is Activity's atlas (Pixelify Sans,
   11–70 px, Regular/Medium, ASCII + Latin-1), generated by Activity's `tools/make-font.py` from
   `ui/assets/rnds/fonts`. No FreeType, no TTF at runtime.
2. **Units, not pixels.** Layout is in 640×480 CSS units; `Canvas::U()` scales positions, borders,
   radii, shadows and the dither step by `panel_width / 640`, so the Plus is the same design at
   1.6×. The atlas picks the nearest baked size.
3. **The primitives are the menu's**: `fillRound`, `fillRoundRing`, `box` (fill, hard-stop bands,
   dither, inset top/bottom, border, hard outer shadow), `tri`, the text engine, and the DRM/evdev
   shim from Activity. A screen is geometry plus styles.
4. **The palette is a table, not constants**: `applyTheme()` sets all `C_*` values (dark/light)
   and the sheet tint; `applyAccent()` sets the five accent shades. `Canvas::box`'s dither reads
   `gLight` so the light theme gets the menu's dark dots, not invisible white ones.
5. **The library is the menu's library**: `systems.conf` (merged at `/run/banana/systems.conf`),
   the two roots, `ext`/`flat`/`folder`, `gamelist.xml`, `media/box`, `game-titles.tsv`,
   `playstats.tsv`, the Store's hidden systems and `shelf.*`. Systems with an absolute folder
   (built-in apps, the Store's Apps shelf) and `banana:` pages are left out; `.app` entries are
   never picks.
6. **The cover is decoded once per pick** with stb_image and fitted to the area; the device's
   memory holds one picture, not a library of thumbnails. A DS icon is read from the banner
   (or the menu's cache) at start.
7. **The launch hand-off is a file.** The menu already watches `/run/banana/launch` and starts the
   game exactly as A on it would; the app writes the path and exits. That keeps the menu in charge
   of releasing and restoring the panels, audio and input, and avoids the app learning the
   launchers' commands.
8. **No idle redraw.** A frame is drawn on input and when the clock minute changes; the main loop
   polls for 500 ms otherwise. A full frame is a few milliseconds at 640×480.

---

## 6. Files

```
store/apps/randomizer/
  banana-randomizer.cpp   the app
  banana-randomizer       the aarch64 static binary (built from the source here)
  randomizer-font.h       Pixelify Sans, baked (generated)
  randomizer-logo.h       the menu's banana at 1x and 1.6x (generated)
  stb_image.h             PNG/JPEG decode
  Makefile                cross and host builds
  README.md               build, install, data, controls, limits
  DESIGN.md               this document
  mockups/                the app's own renders
  tools/
    make-testdata.py      a fake card with six made-up games and covers
    make-font.py          regenerate randomizer-font.h from ui/assets/rnds/fonts
    make-logo.py          regenerate randomizer-logo.h from ui/assets/rnds/logo@*.png
    make-icon.py          the 32x32 die shelf icon
    ppm2png.py            PPM -> PNG
  tests/
    run.sh                the app's checks
    make-card.py          a dependency-free card for them
```

The previews are the app's own output:

```sh
python store/apps/randomizer/tools/make-testdata.py /tmp/card
store/apps/randomizer/banana-randomizer --root /tmp/card --systems /tmp/card/systems.conf \
    --roms /tmp/card/roms --assets ui/assets/rnds --smoke /tmp/r --pick 4
python store/apps/randomizer/tools/ppm2png.py /tmp/r-top.ppm /tmp/r-top.png
```

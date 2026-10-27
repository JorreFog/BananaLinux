# Banana Theme Studio

A theme builder for **BananaOS layout themes** — the themes whose JSON has a `screens` member and draw the whole
menu themselves (`docs/THEMES.md`). It edits the theme JSON directly, evaluates expressions live as you type,
and can preview the theme with the device's own engine.

It works on its own: open `index.html` straight from the disk (or put this folder on any static web host) and
everything works, the live preview included. The preview is the menu's own engine (`ui/src/layout.cpp` and the rest)
compiled to WebAssembly in `wasm/banana-engine.js`, so it draws exactly what the handheld draws, pixel for pixel.
Nothing is uploaded anywhere. `serve.py` is optional: it adds your themes folder and **Send to handheld**. The look is a
plain editor's: Layers, Add, Files and Components on the left, both screens in the middle, the selected element and
the theme's settings on the right, in light or dark (the system's, or the switch up top).

## Quick start

```sh
xdg-open tools/theme-studio/index.html      # or double-click it: the editor and its live preview

python3 tools/theme-studio/serve.py          # optional: + your themes folder (--themes), Send to handheld (--themes-dir)
```

In the editor:

- **File > New theme** (and the first visit) shows the starting points: a simple theme and the examples BananaOS
  ships (Canvas, Hillside, Turntable, Classic), the easiest way in.
- **File > Open file / Open folder** takes a theme's `.json`, a `.zip` with its files, or the `.json` and its pictures
  together; you can also drop any of these on the page.
- **Export** (top right) downloads the theme with every file it uses, ready to unzip into `banana/themes/` on the card;
  **File > Save .json** downloads the theme alone.
- Undo and redo are beside the File menu (Ctrl+Z, Ctrl+Shift+Z); the last theme is kept in the browser and offered
  again next time.
- **Check** lists expression, reference and file problems; **Optimize** finds what makes the theme slow or
  battery-hungry on the handheld and fixes it (below); **Guide** shows `docs/MAKE-A-THEME.md`, `docs/THEMES.md` and
  the colour keys inside the page.

The left side has **Layers** (the elements of the view you pick: Home, Library, Launch or Every view), **Add** (ready
elements), **Files** (the pictures, fonts and sounds the theme uses) and **Components**. The right side edits the
selected **Element**, or the whole **Theme** (name, colours, system icons, buttons, timing). Any field takes a plain
value, or an expression with the small **ƒx** switch beside it; text with `{braces}` in it is filled in live.

The middle is the live preview: both screens, as the handheld draws them. Click it and use the keys (arrows; X or
Enter is A, Z or Backspace is B, S is X, A is Y, Q/W are L/R) or the buttons under it, and tap the screens. Every edit
shows at once. **Preview settings** picks the RG DS's or the RG DS Plus's panel size, the test games or one game for
every system, the battery, Wi-Fi, clock and motion, and takes **stills** (the screens `banana-ui --headless` draws for a
script). The engine's complaints are under **Problems**, in the status bar.

Install a finished theme by putting `my-theme.json` (and its folder of pictures, kept beside it) into
`banana/themes/` on the card, over file sharing or with the card in a computer, then pick it in
**Settings > Display & sound > Theme**.

## Where your themes live

Keep the themes you make in a folder of their own, outside the checkout, so they never appear in the repo or in
the editor's example list:

```sh
python3 tools/theme-studio/serve.py --themes ~/BananaThemes
```

**Theme settings > Your themes** then lists that folder (click to open) and **Save here** writes back into it,
with no downloads and nothing in the repo. With no server, **Open folder** and **Export (.zip)** do the same job. The shipped examples stay under **Start from a template**. (The desktop
studio, `tools/theme-studio-desktop/`, starts its Open and Save dialogs in `~/BananaThemes` too.)

## What it does

- **Layer tree** per view (`home`, `library`, `launch`, `all`) and screen (top/bottom). Children draw in array
  order, so the tree order is the drawing order; reorder with the ↑/↓ buttons or by dragging.
- **Inspector** for the selected element: type, `use`/`with`, the common properties (`x` `y` `w` `h` `right`
  `bottom` `anchor` `ax` `ay` `scale` `opacity` `visible`/`if` `clip` `onTap`), and each type's own (`fill`,
  `gradient`, `radius`, `border`, `shadow`; `text`, `font`, `size`, `align`, `wrap`; `src`, `fit`, `tint`,
  `fallback`; `layout`, `gap`, `padding`, `justify`; `source`, `direction`, `scrollMode`, `item`).
- **Every value** is a literal, an `=expression` or a `{template}` — switch with the Lit/Expr/Tmpl buttons. The
  result is shown beside the field (with a colour swatch when it resolves to one), evaluated with a scope of
  sample systems, games, the clock, the battery and your `vars`.
- **`vars`**, **components** (edited once, instances follow), **states**, **smooth**, gradients, shadows, radii
  and borders.
- **System icons**: every system in `systems.conf` and the collections (favorites, recently played, search), the
  menu's own icon beside the theme's. Choose a PNG or JPEG (at most 4096 px a side) and it is attached as
  `<theme>/icons/<id>.png` and shown in the preview at once; **Reset** puts the menu's icon back. "Show every system
  in the preview" switches to the library with a game for every system, so each icon is on the shelf.
- **Optimize for the handheld**: the desktop studio's rules (`optimize.js`, a port of its `optimize.py`). Findings
  are grouped **Safe** (ticked: the screens look exactly the same; checked with the engine), **Look** (unticked:
  holds animations still, lowers the frame rate, shrinks pictures) and **Advice**. Click one for the details and its
  element; **Apply** is one step of undo. A too-big picture gets a smaller copy beside it, which the theme then names
  and Export carries; the original is left alone.
- **Colour keys** (`colors`, `accent`, `base`, `fonts`, `logo`, `background`, `icons`, `sounds`) — the same
  keys, palettes and accents as `theme-builder.html`, still used to style Settings and the keyboard. **System
  icons** picks a picture per system id from a dropdown (the ids `systems.conf` ships, plus any the running
  server finds in a checkout).
- **Input**, **hints** and **behaviour** (timings, sound cues).
- **Assets**: the pictures, fonts and sounds the theme names are detected from it, with thumbnails and sizes. A web
  page can't read files off the disk by a path, so they are attached: picked here, dropped on the page, or opened
  with the theme as a folder or a `.zip`. Attached files go to the preview, into Export and to **Send to handheld**
  (which writes into a mounted `banana/themes`; start the server with `--themes-dir`).
- **Save** writes the theme JSON with its key order and any members the editor does not know kept. `//` and
  `/* */` comments and trailing commas are read (as the engine reads them); comments are not written back.

## Previews

A preview is not a re-implementation: it is the engine itself. `wasm/build.sh` compiles `ui/src/layout.cpp`,
`expr.cpp`, `json.cpp`, `library.cpp`, `theme.cpp`, `soft.cpp`, `zip.cpp` and the rnds rasterizer with Emscripten,
with `wasm/theme-wasm.cpp` driving them the way `banana-ui --headless` does, into one script with the wasm, the
built-in fonts and icons and `ui/tests/fixture.sh`'s made-up library inlined (a page opened from file:// can't fetch
a `.wasm`). `engine.js` writes the theme and its attached files into the engine's memory and copies its two screens
onto canvases every frame something moves. `tests/engine.test.js` draws every example theme with it and with the
native `ui/banana-ui`, at both sizes, and compares the screens: they are identical.

When `wasm/banana-engine.js` isn't there, the page falls back to `serve.py`'s stills, drawn by running

```
ui/banana-ui --headless OUT --size 640x480 --assets ui/assets/rnds --banana ui/assets/banana \
    --systems system/rootfs/etc/banana/systems.conf --roms <fixture>/roms --data <fixture>/data \
    --theme <theme.json> --script "home 0; wait 900; shot home; press a; wait 900; shot lib; ..."
```

Rebuild the engine after changing it (needs [emsdk](https://github.com/emscripten-core/emsdk) and ImageMagick):

```sh
source ~/emsdk/emsdk_env.sh && sh tools/theme-studio/wasm/build.sh
```

`tests/engine.test.js` says when `banana-engine.js` is older than the sources (`wasm/sources.sh`).

## Files

| File | What it is |
| --- | --- |
| `index.html`, `ui.css` | The page and its look (system fonts, neutral greys, one blue accent; light and dark). |
| `assets/` | The banana icon in the top bar. |
| `app.js` | The editor: tree, inspector, value widgets, colour keys, icons, assets, undo, open/save/export, the preview. |
| `engine.js` | The live preview's client for the WebAssembly engine. |
| `wasm/` | `theme-wasm.cpp` (the engine's binding), `build.sh`, `sources.sh`, and the built `banana-engine.js` (generated). |
| `optimize.js` | Optimize for the handheld (a port of the desktop studio's `optimize.py`). |
| `zip.js` | Reading and writing `.zip` files (Open, Export). |
| `guide.js` | The guides, inlined for the Guide button (generated by `make-templates.py`). |
| `expr.js` | A JavaScript port of the theme expression language (`ui/src/expr.cpp`). |
| `schema.js` | The editor's field metadata: elements, properties, hooks, palettes, the friendly labels and tooltips. |
| `templates.js` | The example layout themes, inlined so "start from a template" works with no server (generated). |
| `make-templates.py` | Rebuilds `templates.js` and `guide.js`; `tests/run.sh` checks they are current. |
| `render.js` | The client for `serve.py`. |
| `serve.py` | Optional: serves the editor, lists `--themes`, writes to `--themes-dir`, stills when there is no wasm. |
| `tests/` | Below. |

`docs/theme.schema.json` (JSON Schema for the format) is the machine-readable companion to `docs/THEMES.md`.

## Testing

```sh
sh tools/theme-studio/tests/run.sh                 # everything below (or make theme-studio from the repo root)
node tools/theme-studio/tests/expr.test.js          # expr.js vs ui/src/expr.cpp, natively and in WebAssembly
node tools/theme-studio/tests/optimize.test.js      # the optimizer's rules, and its safe fixes drawn by the engine
node tools/theme-studio/tests/engine.test.js        # the WebAssembly engine vs ui/banana-ui, pixel for pixel
python3 tools/theme-studio/tests/browser.test.py    # the page from file:// (and a static host) in headless Firefox
python3 tools/theme-studio/tests/serve.test.py      # the server, end to end
sh tools/theme-studio/tests/ui.test.sh              # the page from serve.py in Chromium (skipped without it)
python3 tools/theme-studio/tests/schema.test.py     # the example themes against docs/theme.schema.json
```

`expr.test.js` evaluates `tests/expr-cases.txt` with `expr.js` and with `ui/src/expr.cpp` (`tests/parity.cpp`, and
`bt_eval` in the WebAssembly build), so the port can't drift from the engine. A double that doesn't fit an int is
undefined in C: the handhelds (aarch64) and WebAssembly saturate, x86 gives INT_MIN, so `expr.js` follows the
handhelds and the test lists the few cases where the PC's build answers differently. `browser.test.py` drives
Firefox through its built-in remote control (`tests/marionette.py`), so it needs nothing but Firefox.

## Known limits

- Files are referenced by path and the page only has the ones it is given: open the theme's folder or `.zip`, or
  attach them. One that isn't attached falls back to the engine's default in the preview, as on the device.
- The preview plays no sounds, and START (Settings) and SELECT (search) aren't in it: they are Banana's, not the
  theme's.
- `wasm/banana-engine.js` is 3.6 MB (the fonts are most of it) and has to be rebuilt when the engine changes.
- Comments are dropped on save (the key order and unknown members are kept).
- The preview library is a made-up test one (`ui/tests/fixture.sh`), not your card.
- Element reordering by dragging works within the same parent; use the ↑/↓ buttons for deeper moves.
- The format is still settling for 0.5.0; `docs/THEMES.md` is always the source of truth.
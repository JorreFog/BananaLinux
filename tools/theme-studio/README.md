# Banana Theme Studio

A theme builder for **BananaOS layout themes** — the themes whose JSON has a `screens` member and draw the whole
menu themselves (`docs/THEMES.md`). It edits the theme JSON directly, evaluates expressions live as you type,
and can preview the theme with the device's own engine.

There is no build step. Open `index.html` in a browser to edit offline, or run `serve.py` to also get exact
previews.

## Quick start

```sh
# edit offline — the tree, inspector, live expressions and colour keys all work with no server
xdg-open tools/theme-studio/index.html      # or just open the file in a browser

# edit with exact previews drawn by the real engine
python3 tools/theme-studio/serve.py          # builds ui/banana-ui if it can, opens the page
```

In the editor: **New** starts from a blank theme, **Open** loads a `.json`, **Save** downloads it, **Validate**
lists expression and reference problems. The **Theme** tab lists the example themes that ship on the card as
starting points (when the server is running).

Install a finished theme by putting `my-theme.json` (and any folder of pictures it uses, kept beside it) into
`banana/themes/` on the card — over file sharing, or with the card in a computer — then pick it in
**Settings > Display & sound > Theme**.

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
- **Colour keys** (`colors`, `accent`, `base`, `fonts`, `logo`, `background`, `icons`, `sounds`) — the same
  keys, palettes and accents as `theme-builder.html`, still used to style Settings and the keyboard.
- **Input**, **hints** and **behaviour** (timings, sound cues).
- **Save** writes the theme JSON with its key order and any members the editor does not know kept. `//` and
  `/* */` comments and trailing commas are read (as the engine reads them); comments are not written back.

## Previews

A preview is not a re-implementation: `render.js` posts the theme to `serve.py`, which writes it to a file and
runs

```
ui/banana-ui --headless OUT --size 640x480 \
    --assets ui/assets/rnds --banana ui/assets/banana \
    --systems system/rootfs/etc/banana/systems.conf \
    --roms <fixture>/roms --data <fixture>/data \
    --theme <theme.json> --script "home 0; wait 900; shot home; press a; wait 900; shot lib; ..."
```

and returns the top and bottom PNGs of every `shot`, plus any `banana-ui: theme: ...` lines. So the preview is
exact, and it only changes when the engine does. `--size 1024x768` previews at the RG DS Plus's size.

## Files

| File | What it is |
| --- | --- |
| `index.html`, `ui.css` | The page. |
| `app.js` | The editor: tree, inspector, value widgets, colour keys, save/load. |
| `expr.js` | A JavaScript port of the theme expression language (`ui/src/expr.cpp`). |
| `schema.js` | The editor's field metadata: elements, properties, hooks, palettes. |
| `render.js` | The preview client. |
| `serve.py` | Serves the editor and renders previews with the engine. |
| `tests/` | The expression parity test, the server end-to-end test, and the schema test. |

`docs/theme.schema.json` (JSON Schema for the format) is the machine-readable companion to `docs/THEMES.md`.

## Testing

```sh
sh tools/theme-studio/tests/run.sh                 # everything below
node tools/theme-studio/tests/expr.test.js          # expr.js vs the real engine (needs g++)
python3 tools/theme-studio/tests/serve.test.py      # the preview server, end to end
python3 tools/theme-studio/tests/schema.test.py     # the example themes against docs/theme.schema.json
```

`expr.test.js` is the important one: `tests/expr-cases.txt` is evaluated both by `expr.js` and by a tiny C++
harness over `ui/src/expr.cpp` (`tests/parity.cpp`), and the two are compared, so the JavaScript port cannot
drift from the engine's expressions.

## Known limits

- Assets (pictures, fonts, sounds) are referenced by path; the editor does not upload them, so a preview only
  shows the pictures that already exist beside the theme on the machine running `serve.py`. Files that are not
  there fall back to the engine's default, as on the device.
- Comments are dropped on save (the key order and unknown members are kept).
- The preview library is a made-up test one (`ui/tests/fixture.sh`), not your card.
- Element reordering by dragging works within the same parent; use the ↑/↓ buttons for deeper moves.
- The format is still settling for 0.5.0; `docs/THEMES.md` is always the source of truth.
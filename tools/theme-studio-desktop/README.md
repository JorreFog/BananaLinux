# Banana Theme Studio

A desktop designer for BananaOS layout themes ([docs/THEMES.md](../../docs/THEMES.md)): both screens on a canvas,
elements dragged in from a palette, moved and resized with the mouse, and every property editable as a value or an
`=expression`.

```sh
python3 tools/theme-studio-desktop/theme_studio.py [my-theme.json]
```

Needs Python 3 and PySide6 (`pacman -S pyside6`, `pip install PySide6`). Run from a BananaLinux checkout, it uses the
checkout's fonts and system icons, and can show the real engine's picture (`make ui` first, or Engine > Build
banana-ui). Elsewhere, point it at a checkout with Engine > Set BananaLinux folder.

## What it does

- **Canvas**: the top and bottom screens of a view (home, library, launch). Click to select; Alt-click selects the
  element underneath; drag to move, the handles to resize; arrows nudge (Shift: 10). Positions snap to the screen's
  edges and centre, to other elements and to the grid (hold Shift to stop it). Double-click a text to retype it.
  Clicking an item of a list selects the list's item template, and every item it draws is outlined.
- **Add elements**: shapes, text, pictures, groups, rows and columns, ready-made lists (systems carousel and wheel,
  games list and grid, button hints, page dots) and status pieces (clock, battery, Wi-Fi, launch bar). Drag one onto
  a screen, or double-click it to add it to the selected group.
- **Layers**: each screen's element tree, plus the `all` screens drawn over every view. Drag to reorder or to move
  an element into a group; later in the list is drawn on top.
- **Inspector**: the selected element's properties from THEMES.md's tables, grouped. Each field takes a value, or
  press ƒ to write an `=expression`, which gets completion of the hooks, vars and functions and turns red when it
  doesn't parse. Texts take `{hooks}` in braces. States, `with` parameters, `smooth`, any property the forms don't
  know, and the element's raw JSON are there too.
- **Theme settings**: name, author, accent, default font, variables, the colour-theme keys, fonts, the buttons'
  actions per view, hints and timings.
- **Components**: make one from a selection, edit it once (every `use` follows), add instances.
- **Assets**: pictures, fonts and sounds are copied into a folder named after the theme, next to its JSON, and named
  by relative paths, as the engine expects.
- **Preview**: switch views, move the selection (◀ ▶ A B), scrub `time`, `moved`, `entered`, `launch.progress` and the
  scroll, set the battery and Wi-Fi, and play the animations. The studio draws with its own port of the engine's
  layout rules over a made-up library; **F5** swaps in the real banana-ui's picture (rendered headless in about
  0.1 s, refreshed after each change) and lists the engine's own theme errors under Problems.
- **JSON** tab: the whole file, for anything else. Comments and trailing commas are read; saving writes plain JSON.
- **Export zip**: the theme and its files, to unzip into `banana/themes/` on the card.

## Files

| File | |
| --- | --- |
| `theme_studio.py` | Starts it |
| `app.py` | The window: canvas, panels, file handling |
| `engine.py` | The preview: `ui/src/layout.cpp`'s rules drawn with QPainter, the made-up library, the hooks |
| `expr.py` | `ui/src/expr.cpp` ported: keep the two in step when the language changes |
| `widgets.py` | The inspector's fields and the property tables |
| `presets.py` | The palette's elements and the starting templates |
| `exact.py` | Runs `ui/banana-ui --headless` over `ui/tests/fixture.sh`'s library |
| `jsonc.py` | Reads JSON with comments, writes it in the example themes' style |
| `style.py` | The BananaOS website's look (web/preview): charcoal, cream borders, banana yellow, Pixelify Sans — and its light palette (View > Light theme, Ctrl+T) |
| `tests/smoke.py` | Builds the window offscreen and paints it, so a broken import or stylesheet is caught |

The look matches the web editor (`tools/theme-studio/`), so the two feel like the same tool.

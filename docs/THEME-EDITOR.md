# A web editor for layout themes

BananaOS 0.5.0 adds layout themes: one JSON file describes everything on both screens, so a web editor only has to
read and write that file and preview it. This page links the files that define the format and recommends how to build
the editor around them. The format may still change before 0.5.0 ships; [THEMES.md](THEMES.md) is always current.

> **Built:** [`tools/theme-studio/`](../tools/theme-studio/) is this editor — an offline web app with a server
> (`serve.py`) that previews through the real engine, a JavaScript port of `expr.cpp` kept in step by a parity
> test, and [`docs/theme.schema.json`](theme.schema.json) as the machine-readable format. Its README says what it
> covers and which of the open questions below are settled.

## The files

| File | What it is | Why the editor needs it |
| --- | --- | --- |
| [docs/THEMES.md](THEMES.md) | The format reference: every element, property, hook and expression function | The source of truth for the editor's fields, pickers and autocomplete |
| [themes/canvas.json](../system/rootfs/usr/share/banana/themes/canvas.json) | The example layout theme that ships on the card | A starter template: components, lists, states, the launch view |
| [docs/themes/hillside.json](themes/hillside.json) | A 2000s desktop, all shapes, no pictures | Test file: deep components with parameters, grids, nested groups |
| [docs/themes/turntable.json](themes/turntable.json) | A dark tile launcher with a faked 3D spin | Test file: `time`-driven animation, free lists placed by expressions |
| [docs/themes/classic.json](themes/classic.json) | A bright handheld menu | Test file: status bar hooks, `onTap`, range lists |
| [themes/README.txt](../system/rootfs/usr/share/banana/themes/README.txt) | The colour-theme keys (`base`, `accent`, `colors`, `fonts`, `icons`, `sounds`) | Still apply in a layout theme: they style Settings and the keyboard |
| [themes/theme-builder.html](../system/rootfs/usr/share/banana/themes/theme-builder.html) | The existing one-page colour-theme builder | The current web editor to extend or replace |
| [ui/src/layout.cpp](../ui/src/layout.cpp) | The engine that draws a layout theme | The exact rules: defaults, how boxes, lists and anchors resolve, the hook list (`hook()`) |
| [ui/src/expr.cpp](../ui/src/expr.cpp) | The expression language: parser, operators, every function | Port it to JavaScript for live validation and previews |
| [ui/src/json.cpp](../ui/src/json.cpp) | The JSON reader: comments and trailing commas allowed, key order kept | The editor must keep the order and may write comments |
| [ui/src/theme.cpp](../ui/src/theme.cpp) | Finds themes on the card, applies the colour keys, the pictures' limits | Where files are looked up and which are refused |
| [ui/tests/run.sh](../ui/tests/run.sh), [ui/tests/fixture.sh](../ui/tests/fixture.sh) | The headless renderer's test run and its made-up game library | How to render a theme to PNGs with `banana-ui --headless --theme` |

## How a theme is put together

A theme is a layout theme when it has a `screens` member; without it, it is a colour theme over the built-in Pixel
look. The editor's data model is the JSON itself, so nothing is lost on a round trip.

| Member | What it holds | Editor treatment |
| --- | --- | --- |
| `name`, `author`, `debug` | Shown in Settings; `debug` paints errors on the top screen | Plain fields |
| `base`, `accent`, `colors`, `fonts`, `icons`, `sounds` | The colour-theme keys, still used by Settings and the keyboard | Reuse the existing builder's colour pickers |
| `font`, `vars` | The default font; named values any expression can read | A variables panel; offer the vars in every colour and number picker |
| `components` | Reusable elements; an instance is `{"use": "name", ...overrides, "with": {params}}` | A component library: edit a component once, its instances follow |
| `screens.<view>.top` / `.bottom` | Element trees for `home`, `library`, `launch`, and `all` (drawn over every view) | The main canvas: two 640 x 480 screens and a view switcher |
| `input`, `hints`, `behavior` | Button actions per view, the hint buttons, timings (`launchMs`, `scrollMs`, `fps`, sound `cues`) | A settings form |

**Elements** are `rect`, `text`, `image`, `group` and `list`. Every screen is 480 units high and 640 wide on both
handhelds (the 1024 x 768 panels scale by 1.6). Children draw in array order, so the layer order is the array order.

**Any property value** can be a literal (`20`, `"#202430"`), an expression starting with `=`
(`"=sel ? accent : '#202430'"`) or a template with `{...}` parts (`"{game.plays} plays"`). The editor should keep all
three forms and only offer a visual control when the value is a literal.

**Hooks** are the data an expression reads: `sys.*` and `game.*` (the selection), `item.*`, `i`, `sel`, `pos`, `rel`
(inside a list item), `cursor`, `scroll`, `moved`, `entered`, `time`, `clock`, `date`, `battery`, `online`, `accent`,
`launch.progress` and more. The full list is in THEMES.md and in `hook()` in layout.cpp.

## Live preview

Start with a server that runs the real renderer for exact previews, and move to the same engine compiled to
WebAssembly once the editor works. A JavaScript re-implementation drifts from the device with every engine change.

| Approach | How it works | Fidelity | Speed | Effort |
| --- | --- | --- | --- | --- |
| 1. Server render (start here) | The editor POSTs the theme JSON; a small server writes it to a file and runs `banana-ui --headless OUT --theme file.json --script "home 0; wait 600; shot home; press a; wait 600; shot lib"`, returning the PNGs (top and bottom per shot) | Exact: the device's own code | About 0.1 s a shot after start-up; fine on save or once typing stops | Low: build with `make ui`, the test library from `ui/tests/fixture.sh` |
| 2. WebAssembly (target) | Compile `layout.cpp`, `expr.cpp`, `json.cpp`, `rnds/RndsRaster.cpp` and a canvas backend with Emscripten (FreeType has an Emscripten port); feed it mock systems and games; draw into a `<canvas>` every frame | Exact, and animated | Real time, no server | Medium: a Backend that writes to an ImageData, a JavaScript mock of the library |
| 3. JavaScript renderer | Re-implement the elements in canvas or the DOM | Close, never exact (text layout, picture fits, easing) | Real time | High, and every engine change must be repeated |

Whichever renderer is used, port `expr.cpp` to JavaScript (about 450 lines) or call it from the WebAssembly build: the
editor needs it anyway to evaluate expressions inline, show their current value next to the field and flag errors as
they are typed. The headless renderer also takes `press`, `touch` and `wait` commands, so the preview can simulate the
buttons.

## What the editor should support

- [ ] A layer tree per screen and view (`home`, `library`, `launch`, `all`), reordered by dragging (array order is
  drawing order)
- [ ] An inspector per element type, built from the property tables in THEMES.md: position (`x`, `y`, `w`, `h`,
  `right`, `bottom`, `anchor`, `scale`), `opacity`, `visible`, `clip`, then the type's own (`fill`, `gradient`,
  `radius`, `border`, `shadow`; `text`, `font`, `size`, `align`, `wrap`; `src`, `fit`, `tint`; `layout`, `gap`,
  `padding`; `source`, `direction`, `scrollMode`, `item`)
- [ ] Every field takes a literal, an `=expression` or a `{template}`, with a switch between a visual control and
  code, and autocomplete for hooks, vars and functions
- [ ] Lengths as numbers or percentages (`"50%"`); a radius as one number or four corners
- [ ] Components: made from a selection, edited once, their instances listed; `with` parameters shown as fields on
  each instance
- [ ] States: a list of `{ "when": ..., overrides }` per element, previewed by switching the condition
- [ ] `smooth` per property, and a scrubber for `moved`, `entered`, `time` and `launch.progress` so animations can be
  previewed frame by frame
- [ ] Lists: pick the source, then edit the `item` template once with mock items around it
- [ ] Assets: PNG or JPEG pictures (at most 4096 px a side), TTF fonts and WAV sounds, kept relative to the theme file
- [ ] Unknown members and the key order kept when saving, so themes written by hand or for a newer version survive a
  round trip
- [ ] The four example themes offered as templates

## Validation, export and installing

**Validation.** The engine never refuses a theme over a bad property: it draws what it can and logs each problem as a
`banana-ui: theme: ...` line (a server render can return these with the PNGs). Show them next to the element they
name; the message says where (`screens.home.bottom[2](list).x: ...`). Check in the editor too: an unknown `type`, a
`use` naming a missing component, an expression that does not parse, a file that is not there.

**Export.** A theme is `my-theme.json` plus a folder of its files beside it, named with relative paths
(`"src": "my-theme/wallpaper.png"`). Export a zip holding both.

**Installing.** Unzip into `banana/themes/` on the card (over the network with Settings > Network > File sharing, or
with the card in a computer), then pick the theme in Settings > Display & sound > Theme, where it shows under its
`name`.

**Reloading.** The handheld reads a theme when it is picked in Settings, so a later "send to the handheld" button can
copy the files over file sharing and ask for the theme to be picked again.

## What BananaOS can add for the editor

- A JSON Schema (`docs/theme.schema.json`) made from the property tables, for validation and autocomplete
- A `"format": 1` member, so the editor knows which version of the format a theme was written for
- A way for the headless renderer to read the theme from standard input instead of a file
- A WebAssembly build target in `ui/Makefile`, if the editor goes that way

Open questions:

- Does the editor extend `theme-builder.html` (one offline page, no server) or start again? A server render needs a
  host; WebAssembly keeps it offline.
- Which of the engine's known limits (rotation, ellipses, column-wise list wrapping, next and previous hooks) should
  come first? The editor's needs can set that order.

# Layout themes

A layout theme draws the whole menu from its JSON file. The engine supplies the data (systems, games, the selection,
the clock, the battery...), the buttons and the drawing. Where things go, how they look and how they move is up to
the theme. A theme is a layout theme when it has a `"screens"` member. Otherwise it is a colour theme over the Pixel
look (see `/usr/share/banana/themes/README.txt`).

> **New to themes?** [MAKE-A-THEME.md](MAKE-A-THEME.md) is a gentler introduction: changing colours, using the
> theme editor, your own pictures and fonts, and putting it on the card. This page is the exact reference.

`canvas.json` in `/usr/share/banana/themes` (copied to `banana/themes` on a new card) is a complete example. Copy it
and change it.

## Trying a theme on a computer

`make ui` builds banana-ui for the computer. This renders a theme headless to PNGs:

```sh
ui/tests/fixture.sh /tmp/fx      # a test library of made-up games
ui/banana-ui --headless /tmp/shot- --size 640x480 --assets ui/assets/rnds --banana ui/assets/banana \
    --systems system/rootfs/etc/banana/systems.conf --roms /tmp/fx/roms --data /tmp/fx/data \
    --theme my-theme.json --script "home 0; wait 1000; shot home; press a; wait 1000; shot lib"
```

Each `shot` writes `<prefix><name>-top.png` and `-bot.png`. Theme errors are printed as `banana-ui: theme: ...`. With
`"debug": true` in the theme they are also shown in red on the top screen.

## The file

```jsonc
{
  "name": "My theme",
  "base": "dark", "accent": "#7c9cff", "colors": { ... }, "fonts": { ... },   // as a colour theme: the Settings screen
  "font": "inter",              // the default font of text elements
  "stage": "screen",            // lay out on each screen's own size (below); default "canvas"
  "panels": "flat",             // Settings' boxes flat with a hairline edge (below); default the Pixel look
  "settings": { "top": "open", "left": 58, "icons": "tile", "pick": "outline", "hints": "pill", "header": "rule",
                "bodyFont": "inter" },   // Settings' own pages laid out the theme's way (below)
  "transitions": "fade",        // eased fades between the boot, the menu, a game and the shutdown (below)
  "vars": { "bg": "#10131c", "hi": "=accent" },          // names for values, usable in any expression
  "components": { "chip": { ...an element... } },        // reusable elements ("use": "chip")
  "input": { "library": { "down": "next", "up": "prev" } },  // what the buttons do (below)
  "hints": { "home": [["A", "Open"], ["Y", "Favorite"]] }, // the "hints" list's buttons per view
  "systems": { "nds": { "short": "DS", "year": "2004" } }, // the theme's own words for systems: sys.short, sys.year
  "behavior": { "launchMs": 900, "scrollMs": 70, "settleMs": 600, "fps": 30, "wrap": false,
                "extraHints": true, "cues": { "insert": 560 } },
  "screens": {
    "home":    { "top": [ ...elements... ], "bottom": [ ... ] },
    "library": { "top": [ ... ], "bottom": [ ... ] },
    "launch":  { "top": [ ... ], "bottom": [ ... ] },   // optional: the library's screens are used if it is missing
    "all":     { "top": [ ... ] },                      // drawn over every view (a status bar)
    "settings": { "top": [ ... ], "bottom": [ ... ] }   // drawn under Settings' own pages (below)
  }
}
```

`//` and `/* */` comments and trailing commas are allowed.

Each screen is 480 units high and 640 wide on both handhelds. The RG DS Plus's 1024 x 768 panels scale it by 1.6,
and text and shapes are drawn sharp at that size.

### Screens of another shape (the AYN Thor)

The AYN Thor's top screen is 16:9 (1920 x 1080) and its bottom one nearly square (1240 x 1080). By default a theme still
lays out on the 640 x 480 canvas, and the canvas is placed in the middle of each screen, as large as it fits: the top
has 107 units more on each side, the bottom 39 more above and below. Nothing is cut off there: an element can go past
the canvas's edges into the rest of the screen. `screenX`, `screenY`, `screenW` and `screenH` are the whole screen in
the canvas's units (on the Thor's top `-106.7, 0, 853.3, 480`), so `{ "type": "rect", "x": "=screenX", "y": "=screenY",
"w": "=screenW", "h": "=screenH", "fill": "#000" }` fills it. On the RG DS they are the canvas, `0, 0, 640, 480`.

With `"stage": "screen"` the theme lays out on each screen's own size instead: `W` and `H` are the screen's (853.3 x 480
on the Thor's top, 640 x 557.4 on its bottom; 640 x 480 on the RG DS), and 0,0 is its top-left corner. A theme
written with `W` and `H` (`"x": "=W / 2"`, `"y": "=H * 0.5"`, `"w": "100%"`) fills every shape. `vars` are worked out
for each screen, so a var can read `W` and `H`. `thor-oled.json` is one: the AYN Thor starts in it (the board's default
for `theme` in `settings.conf`, `user:thor-oled`; the RG DS keeps Dark). Its measures are the design's, in the
panels' pixels: its vars `u` and `v` are the units a pixel of the bottom and of the top screen (`"x": "=160 * v"`), so
it shrinks to fit the RG DS's 4:3 panels.

The Thor's bottom screen is narrower than the canvas, so the canvas is drawn smaller on it (1 unit is 1.9375 px
there, 2.25 on the top). A layout theme's boxes, text and pictures are made for each screen's own pixels: nothing is
drawn at one size and scaled to another, and pixel art stays whole multiples of its pixels on both.

## Settings, the setup and the moments between screens

Settings, the first-time setup, the lists (networks, languages), the keyboard, the dialogs, the game panel and the quick
menu are drawn by the engine, not from `screens`. A theme still decides how they look:

| `settings.` | |
|---|---|
| `top` | `"open"`: the top screen's text straight on the screen, no card round it |
| `left` | the top screen's text column's left edge, in units from the screen's own left edge (on the Thor's 16:9 top, where the 4:3 canvas would leave it in the middle) |
| `icons` | the top screen's pixel icons: `"tile"` on a lit tile as the home screen's system, `"none"` left out |
| `pick` | `"outline"`: the picked row stays the surface with an edge and a glow in the accent, its text the ink (default: filled with the accent) |
| `hints` | `"pill"`: the B / A hints as pills with a round key, A's edge and label in the accent |
| `header` | `"rule"`: a page's title over a hairline instead of in a box |
| `bodyFont` | the help texts' font: `inter`, `inter-medium`, `inter-semibold` or a `.ttf` beside the theme (default: `fonts.regular`) |

`screens.settings` (a layout theme's) is drawn over each screen's background under those pages: `thor-oled.json`
puts its status bar there, so Settings and the setup have the same header as the shelf.

`"transitions": "fade"` replaces the iris and the stepped fades with eased cross-fades of the whole screens: the boot
animation into the menu (260 ms), a game ending (the menu out of black, 220 ms), the first boot's arrival ("Welcome
to" rising over the boot animation's last frame, then black), the setup in and the Welcome card into the menu, and the
menu into the shutdown (200 ms). On screens of another shape than the canvas (the AYN Thor's) they are used whatever
the theme, since the iris works on the canvas. A game starts after `behavior.launchMs` (Thor OLED: 290 ms: the plate under the game is
pressed, then the menu rises away and fades).

## Values: literals, expressions, templates

Any property can be:

- a literal: `"x": 20`, `"fill": "#202430"`, `"text": "Systems"`;
- an expression, starting with `=`: `"x": "=320 + pos * 160"`, `"fill": "=sel ? accent : '#202430'"`;
- a template, with expressions in braces: `"text": "{game.plays} plays · {game.time}"` (`{{` for a brace).

Expressions have numbers, strings (`'...'` or `"..."`), colours (`#abc`), `+ - * / %` (`+` joins strings),
`== != < <= > >=`, `&& || !`, `a ? b : c`, and these functions:

| | |
|---|---|
| maths | `min max clamp(v,lo,hi) abs floor ceil round(v,digits) sqrt pow sign mod(a,b) sin cos sind cosd lerp(a,b,t) step(edge,x) smoothstep(a,b,x)` |
| motion | `progress(ms_ago, duration)` 0..1, `easeIn easeOut easeInOut easeOutBack easeOutElastic bounce` (of 0..1), `pulse(time, period)` 0..1..0 |
| colour | `mix(c1, c2, t)`, `alpha(c, a)`, `lighten(c, t)`, `darken(c, t)`, `rgb(r,g,b)`, `rgba(r,g,b,a)`, `hsl(h,s,l[,a])` |
| text | `upper lower len substr(s,from,n) contains(s,part) replace(s,a,b) pad(n,width[,char]) fixed(n,digits) plural(n,'game') str num` |
| choice | `if(c, a, b)`, `pick(i, a, b, c...)`, `either(a, b, ...)` (the first one that isn't empty) |

Colours are `#rgb`, `#rgba`, `#rrggbb`, `#rrggbbaa`, `rgb(...)`, `rgba(...)`, `transparent` or a few names.

## Hooks: what the expressions can read

| name | |
|---|---|
| `view` | `"home"`, `"library"` or `"launch"` |
| `W`, `H`, `screen` | the screen's size in units (640 x 480; with `"stage": "screen"` its own), 0 for the top screen and 1 for the bottom one |
| `screenX`, `screenY`, `screenW`, `screenH` | the whole screen in the layout's units: past the canvas on a screen of another shape (above) |
| `time` | ms since the menu started. Using it redraws continuously at `behavior.fps`, so use it only for things that really move all the time |
| `clock`, `date`, `day` | `"13:01"` / `"01:01 PM"` (Settings' clock), `"10/02"`, `"Thu"` |
| `monthDay`, `month` | the date's parts, for a theme that writes it its own way: `12`, `"Nov"` (`"{day} {monthDay} {month}"`) |
| `battery`, `charging`, `batteryMode` | percent (-1 unknown), 1 on the charger, `"icon"` / `"text"` (Settings) |
| `online`, `motion`, `brand` | Wi-Fi up, Settings' motion, `"Alpha 0.4.1"` |
| `paused` | the title of the game stopped behind the menu (a handheld with a Home button, pressed in a game), `""` when there is none |
| `accent`, `accent.light/.dark/.band/.low` | the accent colours (Settings' accent, or the theme's) |
| `palette.<name>` | a colour of the palette (`palette.ink`, ...) |
| `cursor`, `count`, `scroll` | the selection's index and the count in the current list (systems at home, games in a library); `scroll` is the selection's eased position, a fraction while it moves |
| `moved`, `entered`, `dir` | ms since the selection last moved, ms since the view changed, the last move's direction (1 / -1) |
| `launch.progress`, `launch.t` | 0..1 and ms through the launch view |
| `empty`, `empty.title`, `empty.hint` | a library with no games, and what to say about it |
| `systems.count`, `games.count` | |
| `sys.*` | the selected system: `id name maker color icon games played time fav app hasResume resumeTitle resumeEyebrow resumeWhen resumeArt resumeBox resumeCart resumeLabel recent1Art recent2Art index`, and the theme's own `systems` words (`sys.short`) |
| `game.*` | the selected game (library and launch): `title sub plays last time art box cart label icon rom romIcon achA achB achLabel achProgress fav index systemId systemName` |
| `item.*`, `i`, `n`, `sel`, `rel`, `pos`, `first`, `last`, `ix`, `iy` | inside a list item: its own system's or game's fields (as above; `key label` for hints), its index, the count, whether it is selected, its distance from the selection (`rel` whole, `pos` eased), and where the list put it |
| your `vars`, a component's `with` | |

`sys.resumeWhen` is when the last game was played (`"Today"`, `"Yesterday"`, `"Last week"`..., `""` never);
`sys.recent1Art` and `sys.recent2Art` are the covers of the two games played before it (else the shelf's first ones
with a cover), for a theme that shows more than one. The theme's `"systems": { "<id>": { "<key>": "..." } }` adds its
own hooks by system: `sys.<key>` and, in a list, `item.<key>` (`""` for a system it doesn't name; Thor OLED's short
names for its tab strip and the years beside the makers).

Pictures from hooks (`sys.icon`, `game.art`...) are full paths. A theme's own files are relative to its JSON file.
`game.romIcon` is a DS game's own icon from its ROM.

## Elements

An element is an object with a `type`: `rect`, `text`, `image`, `group` or `list`. Without a `type`, it is a group if
it has `children`, text if it has `text`, an image if it has `src`, a list if it has `source`, and a rect otherwise.
An array of elements is a group of them.

### Common properties

| | |
|---|---|
| `x`, `y`, `w`, `h` | position and size in the parent, in units or `"50%"` of the parent. A rect or group without `w`/`h` fills its parent, and text is as big as its lines |
| `right`, `bottom` | distance from the parent's right or bottom edge (with `x` and no `w`: stretches) |
| `anchor`, `ax`, `ay` | which point of the element `x`/`y` place: `"center"`, `"top right"`, `"bottom"`..., or 0..1 |
| `scale` | grows or shrinks the element and its children about its anchor |
| `opacity` | 0..1, multiplied into its children |
| `visible` / `if` | false: not drawn (nor its children) |
| `clip` | children cut to the element's box |
| `states` | `[{ "when": "=sel", "fill": "#fff", "scale": 1.1 }, ...]`: properties that replace the others while `when` holds (the last one wins) |
| `smooth` | `{ "x": 120, "scale": 90 }` (time constants in ms), or one number for x, y, w, h, opacity and scale. The property eases towards its value instead of jumping |
| `onTap` | a button the element acts as when tapped: `"a"`, `"b"`, `"left"`... |
| `use`, `with` | a component, with the element's own properties over the component's; `with: { "label": "Play" }` sets names for its expressions |

### rect

`fill` (or `color`); `gradient: { "angle": 180, "stops": ["#000", "#123 40%", "#fff"] }` (CSS angles: 180 runs top to
bottom); `radius` (a number, `"50%"` for a pill or a circle, or `[tl, tr, br, bl]`); `border` and `borderColor` (or
`border: { "width", "color" }`); `shadow: { "x", "y", "blur", "spread", "color" }`.

### text

`text`, `font`, `size`, `color`, `align` (`left`, `center`, `right`, within `w`), `valign` (`top`, `middle`, `bottom`,
within `h`), `upper`, `spacing` (letter spacing), `wrap`, `maxLines`, `lineHeight`, `ellipsis` (default on: a line
longer than `w` ends in ...), `shadow: { "x", "y", "color" }`, `outline: { "width", "color" }`.

Fonts are named in the theme's `fonts` (`"title": "mine/Title.ttf"`) or are built in: `pixel`, `pixel-medium`
(Pixelify Sans), `inter`, `inter-medium`, `inter-semibold`, `inter-bold`.

### image

`src`, `fallback` (another source when `src` is empty or doesn't load), `fit` (`cover`, `contain`, `fill`, `coverTop`,
`pixel` stretched without smoothing, `pixelContain` whole multiples of its pixels, `tile` repeated at its own pixel
size: a texture), `radius`, `tint` (multiplied),
`crop` (`cardLabel`: a real DS card scan's artwork, `noStrip`: a DS box without its white strip), `placeholder` (a
colour shown while it loads or when there is nothing). Pictures are decoded and fitted off the main thread.

What can be done to the fitted picture (once, when it is made; an animated value makes it again each frame):

| | |
|---|---|
| `blur` | blurred by this much (units; the deviation of a Gaussian blur, as CSS's `blur()`). With a low `opacity` and a `mask`, a cover becomes a soft light of its own colours |
| `saturate` | its colours stronger (above 1) or greyer (0 is grey) |
| `mask` | it fades out: `"radial"` from its middle to its nearest side, or `{ "shape": "linear", "angle": 90, "from": 0.2, "to": 0.9 }`: whole up to `from`, gone at `to` (0..1 of the way; a radial one takes `from` and `to` too) |
| `rotate`, `pivot` | turned by degrees, clockwise, about `pivot` (`[0.5, 1]`: the middle of its bottom edge; default its middle) |
| `tilt`, `perspective`, `pivot` | swung by degrees round the upright line through `pivot`, as a door (CSS's `rotateY`), seen from `perspective` units away (default 1000): `"tilt": 9, "pivot": [0, 0.5]` is a game's case seen a little from the side |

Only the picture turns: an image's children and the boxes round it stay upright.

### group

Children, drawn in order. It can paint a box behind them like a rect (`fill`, `gradient`, `radius`, `border`,
`shadow`). `layout: "row"` or `"column"` places the children one after another: `gap`, `padding`, `justify`
(`start`, `center`, `end`, `between`), `align` (across: `start`, `center`, `end`). A row or column without `w`/`h` is
as big as its children (a pill as wide as its text).

### list

One `item` element repeated for each entry of a `source`: `systems`, `games` (the current library), `hints` (the
buttons of `hints` plus Banana's own, such as SELECT Search), or `range` (`count` items with only `i`).

| | |
|---|---|
| `direction` | `row`, `column`, `grid` (`columns`), or `free`: the item places itself from `pos`/`rel` (a carousel, a wheel, a fan: `"x": "=320 + pos * 140", "scale": "=1 - abs(pos) * 0.1"`) |
| `itemW`, `itemH`, `gap`, `stepX`, `stepY` | the items' size and spacing |
| `scrollMode` | `center` (the selection stays in the middle), `edge` (scrolls when it would leave the box), `none` |
| `loop` | the list wraps around (with `behavior.wrap` for the buttons) |
| `flow` | each item as wide (row) or high (column) as its own content: hint chips |
| `range` | a free list draws the items this far from the selection (8) |
| `selectedOnTop` | the selected item drawn last, over its neighbours (default true) |

Only the items in view are drawn, so a list of a thousand games costs what the visible ones cost. Tapping an item
selects it, and tapping the selected one again opens it.

## Buttons

`input.<view>.<button>` (`home` and `library`; buttons `left right up down a b x y l r`) is one of: `prev`, `next`
(`prev:5` moves 5: a grid's rows), `first`, `last`, `open` (home: the system, or starts an app; library: the game),
`back`, `fav`, `resume` (home: the system's last game), `prevSystem`, `nextSystem`, `none`. START (Settings) and
SELECT (search) stay Banana's. The defaults are the Carts theme's: home left/right/up/down/L/R move, A opens, X
resumes, Y stars; library left/right move, up/down move 10, L/R change system, A plays, B goes back, Y stars.

A starts the `launch` view, which lasts `behavior.launchMs`. Its `cues` play the theme's sounds at those ms (`insert`).
Then the game starts.

## Speed

The menu redraws only while something moves: the eased scroll, `smooth` values, `behavior.settleMs` after a move or
a view change (for the theme's `progress(moved, ...)` animations), the launch, or a theme that reads `time`. Shapes,
text and fitted pictures are made once and kept, and the screens' unchanged parts aren't copied again.

## What it can't do (yet)

- Rotation and skew of anything but a picture (an image's `rotate` and `tilt`): boxes, text and groups stay upright
  rectangles.
- Blur of what's behind an element (an image's `blur` blurs the picture itself), and blend modes other than normal.
- Masks other than rounded corners.
- Dragging a list with a finger (a tap selects).
- Lists of other things than systems, games and hints (achievements, the settings).
- Settings and the keyboard keep their own layout (the theme's `colors`, `accent`, `fonts` and `settings` above apply to them, and
  `"panels": "flat"` makes their boxes flat: the surface's colour `panelBody` (`bubbleBody` for the bubbles), a
  hairline edge in `edge` (`line` for the bubbles, the accent's on the one that's on), a light line along the top in
  `panelBand` and a darker one along the bottom in `panelLow`; the picked row in the accent, `onPick` on it; the
  screens `screenBg` with the dots only when `background.dither` isn't false).
- Glow is a box's `shadow` with no fill (a blur round its shape, in the shadow's colour): pictures and text don't
  glow, and nothing behind an element is blurred.
- 3D: the Carts theme's rendered cartridges aren't available as an element.

# Make your own theme

A theme changes how the menu looks: its colours, its pictures, its sounds — and, if you want, where everything is
and how it moves.

You do not have to write any code, and you cannot break anything. A theme is one text file that only changes the
menu, only when you pick it. Start by changing a colour in it; stop there if that is all you wanted.

1. **[Change the colours](#1-the-quick-one-change-the-colours)** — a few lines, five minutes.
2. **[Rearrange the menu](#2-the-big-one-rearrange-the-menu)** — drag things around in the theme editor.
3. **[Share it](#6-sharing-your-theme)** — a zip and a message.

## Before you start

**Where themes live.** On the card, in `banana/themes/` — the `BANANA` drive that appears when you plug the
handheld into a computer. The handheld's files are easy to get at over the network: open `\\<address>\banana` in
Windows' File Explorer, or `smb://<address>/banana` on a Mac.

**You cannot break anything.** Pick **Dark** in **Settings ▸ Display & sound ▸ Theme** and you are straight back to
the normal look. If a theme file cannot be read, the menu quietly uses Dark and writes down why in
`banana/logs/banana-ui.log`.

**Work on a copy.** A new card already has the themes BananaOS ships in `banana/themes/` — `ocean.json`,
`sakura.json`, `terminal.json`, `sunset.json` and the layout theme `canvas.json` — and the originals live in
`/usr/share/banana/themes` on the handheld. Copy one, rename it, and edit that. A file on the card with an
example's name replaces that example.

**Picking it up.** **Settings ▸ Display & sound ▸ Theme** lists every theme by its `name`. If you change a file
the menu is already using, pick the theme again to reload it.

## 1. The quick one: change the colours

Copy `ocean.json` to `cherry.json` and change what it says:

```jsonc
{
  "name": "Cherry",        // what Settings shows in the list
  "author": "you",
  "base": "dark",          // start from the dark menu, or "light"
  "accent": "#e04f6d"      // the selection, the logo and the boot banana
}
```

Save it into `banana/themes/`, pick **Cherry**, and the menu's highlights are cherry red. That is a whole theme
already, and `accent` is the single colour that changes the most — try that first.

When you want more, `colors` replaces any one colour of the menu:

```jsonc
{
  "name": "Cherry",
  "base": "dark",
  "accent": "#e04f6d",
  "colors": {
    "screenBg": "#1b0d12",   // the background
    "ink": "#ffe9ee"         // the text
  }
}
```

`banana/themes/README.txt` on the card (and beside it in this repository) lists every colour and what it paints:
the screen, the text, the panels, the shelf, the box art, the keyboard. Colours are written `#rgb`, `#rrggbb`, or
`#rrggbbaa` — the last two digits are how see-through it is, `00` clear to `ff` solid.

The same colours style **Settings and the on-screen keyboard**, so a theme with a pale background wants its text
colour dark, or the settings will be hard to read.

### Your own pictures, fonts and sounds

A colour theme can name its own files too:

```jsonc
{
  "background": { "top": "cherry/top.png", "bottom": "cherry/bottom.png" },
  "logo": "cherry/logo.png",
  "icons": { "nds": "cherry/ds.png", "gba": "cherry/gba.png" },
  "fonts": { "regular": "cherry/MyFont.ttf", "medium": "cherry/MyFont-Medium.ttf" },
  "sounds": { "scroll": "cherry/tick.wav", "fav": "off" }
}
```

Keep them in a folder named after the theme, beside its `.json` — `cherry/cherry.json`, `cherry/top.png` — and
name them like `"cherry/top.png"`.

| | |
|---|---|
| **background** | pictures behind the two screens, scaled to fill |
| **logo** | the picture at the top of the bottom screen |
| **icons** | one picture per system, by its id (`nds`, `gba`, `gb`, `snes`, `ports`, ...) |
| **fonts** | TTF files: the menu's text and its titles |
| **sounds** | WAV files for scrolling, selecting and so on; `"off"` for none |

Pictures are PNG or JPEG, at most 4096 pixels a side. A file that is missing simply keeps the normal look for that
one part. Sounds change the next time the menu starts.

## 2. The big one: rearrange the menu

Everything so far changes colours and files. A theme that *also* has a `screens` section draws the whole menu
itself: what is on each screen, where it is, how big, what colour, and how it moves. `canvas.json` is one, and it
is the best thing to read while you learn.

You do not have to hand-write it. **Use the editor.**

## 3. The theme editor

There are two versions, and they look and work the same. Take whichever suits you.

**In a browser**

Open **<https://jorrefog.github.io/BananaLinux/theme-studio/>**: nothing to install, and nothing you make leaves
your computer (the page runs on its own once loaded). Or offline, from a checkout:

```sh
xdg-open tools/theme-studio/index.html      # or just double-click the file
```

Everything works straight from the file, with nothing to install, including a **live preview drawn by the menu's
own engine**: both screens, exactly as the handheld draws them, which you can drive with the keys and the mouse. The
folder also works from any web host. Open a theme together with its pictures (its folder, or a `.zip`), and
**Export (.zip)** gives you the theme and its files ready to unzip into `banana/themes/`. The little server,
`python3 tools/theme-studio/serve.py`, is only needed for **Send to handheld** and a themes folder of your own.

**As a desktop app**

```sh
python3 tools/theme-studio-desktop/theme_studio.py
```

It needs Python 3 and PySide6 (`pip install PySide6`).

### What you do in it

- **Pick a screen.** The menu has a *home* view (your systems), a *library* view (the games in one system), a
  *launch* view (the moment a game starts) and *all* (a bar drawn over every view). Each has a top and a bottom
  screen. The buttons at the top move between them.
- **Click something to change it.** The panel at the side shows everything about whatever is selected: its
  position and size, its colour, its text, its font. Change one and the preview updates. The desktop editor draws
  both screens on a canvas, so you can also drag things about and resize them with the mouse; they snap to the
  screen's edges and centre, to each other and to a grid.
- **Add new things** from the palette: shapes, text, pictures, groups, ready-made lists (a games grid, a system
  carousel, the button hints) and status pieces (the clock, the battery, the Wi-Fi signal).
- **Reorder** in the layer list: later in the list is drawn on top.
- **Save** writes the theme's `.json`. Then copy it — and its folder, if it has one — into `banana/themes/`.

The exact preview is the menu's own engine drawing your theme — not the editor's redrawing of it — so what you see
is what the handheld shows.

**Optimize** finds what makes a theme slow or hard on the battery — usually something that keeps a
screen redrawing when nothing happens, like a twinkle that reads `time` — and fixes it: the safe fixes change nothing
you can see, the others are yours to pick.

**Keep your themes out of the repository.** Both editors start their Open and Save dialogs in `~/BananaThemes`,
which is where your themes should live on your computer, so they never turn up in a commit. The web editor can be
pointed elsewhere with `python3 tools/theme-studio/serve.py --themes ~/BananaThemes`.

## 4. How a theme is put together

In plain words, since it helps to know what you are looking at.

**Screens.** Two of them, called *top* and *bottom*. Think of each as a sheet 640 wide and 480 tall. Every
position and size in a theme is in those units, so `"x": 320, "y": 240` is the middle of a screen. (The RG DS
Plus's bigger panels are just this sheet scaled up; nothing in the theme changes.)

**Elements.** A screen holds a list of them, drawn in order — the first is at the back, the last on top:

| | |
|---|---|
| **a shape** | a rectangle: a flat colour, a gradient, rounded corners, a border, a shadow |
| **text** | a line of words: the font, the size, the colour, whether it wraps |
| **a picture** | a file, fitted inside its box |
| **a group** | a box that holds other elements — this is how you build a panel with things on it |
| **a list** | one element, repeated for each entry of something: your systems, the games in a library, the button hints |

**Lists** are what make a theme alive. Write the element once and the menu repeats it for every system or game,
telling it which one it is drawing. That is how a shelf of games is one element in the file.

**"If this, then that".** Any value in a theme can be worked out on the spot instead of being fixed. An expression
starts with `=`, and `{}` drops one into a piece of text:

```jsonc
// inside an element, in the theme file
"fill":  "=sel ? '#000080' : '#c0c0c0'",     // navy when this one is selected, grey otherwise
"text":  "{game.title}",                      // the name of the game being drawn
"scale": "=sel ? 1.1 : 1"                     // a little bigger when selected
```

`sel` is "is this one selected", `game.title` is the selected game's name, and so on. As you type, the editor
shows what the expression comes to beside the field; the desktop editor also completes the names for you.

**Names for values and reusable pieces.** `vars` gives a name to a value you use in several places
(`"ink": "#101010"`), and `components` is a piece you design once and drop in as many times as you like — change
the component and every copy follows.

## 5. Trying it on the handheld

Copy your `.json` (and its folder, if it has one) into `banana/themes/` on the card, then pick it in
**Settings ▸ Display & sound ▸ Theme**. If you changed a file the menu is already showing, pick the theme again.

To work on a theme without unplugging anything, turn on **Settings ▸ Network ▸ File sharing**: the card appears on
your Wi-Fi as a network folder, so you can copy your files straight in — or let the editor's **Send to handheld**
write them for you.

## 6. Sharing your theme

A theme is its `.json` plus its folder, if it has one. Zip those together and post the zip — that is what the
themes in [banana-themes](https://github.com/jesussaves-ai/banana-themes) are.

Worth doing: put a `name` and an `author` in the file, and say which BananaOS it was made for. Pictures and fonts
are your own or openly licensed — a lot of the old console and computer fonts are not.

## 7. When something looks wrong

**The theme is not in the list.** The file has to be named `something.json` and sit directly in `banana/themes/`,
not in a folder inside it.

**The menu looks like Dark.** The theme could not be read — a missing comma, usually. The reason is written in
`banana/logs/banana-ui.log` on the card.

**One part of it is wrong.** Add `"debug": true` near the top of the theme and the menu paints each problem in red
on the top screen, saying where it is. Take it out when you are done.

**Text is hard to read, or something is off the screen.** Check the two colours you are putting together (a grey
on a grey) and the position you typed (280 is fine, 680 is off the 640-wide screen).

**A list looks empty.** An empty library is not a bug: that system has no games in `roms/` on the card.

## 8. Where to go next

- **[THEMES.md](THEMES.md)** — every element, every property, every expression function. The full reference.
- **[canvas.json](../system/rootfs/usr/share/banana/themes/canvas.json)** — a complete example layout theme, and
  the best starting point to copy.
- The other examples: [hillside.json](themes/hillside.json) (all shapes, no pictures),
  [turntable.json](themes/turntable.json) (moves on its own), [classic.json](themes/classic.json) (a status bar).
- **[themes/README.txt](../system/rootfs/usr/share/banana/themes/README.txt)** — the colour keys, by what they
  paint.
- **[THEME-EDITOR.md](THEME-EDITOR.md)** — for anyone building their own editor rather than a theme.
- **[theme.schema.json](theme.schema.json)** — the format as a machine-readable schema, if you would rather have
  your editor check it for you.

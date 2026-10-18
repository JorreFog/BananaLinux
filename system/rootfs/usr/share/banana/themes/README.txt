BANANA THEMES
=============

A theme is one text file in JSON, in banana/themes on the card (file sharing, or the card in a computer):
for example banana/themes/my-theme.json. Pick it in Settings > Display & sound > Theme, where it shows under its
"name". To start, copy one of the examples (ocean.json, sakura.json, terminal.json: they are in the system's
/usr/share/banana/themes, and copies are in banana/themes on a new card) and change it. A file on the card with an
example's file name replaces that example.

    {
      "name": "My theme",          the name shown in Settings (else the file name)
      "author": "you",             (shown nowhere yet)
      "base": "dark",              the palette it starts from: "dark" or "light"
      "accent": "#3cc6e8",         the selection, the logo, the boot banana: one colour (its lighter and darker
                                   shades are worked out), one of Banana's names ("yellow", "blue", "red",
                                   "green", "purple", "pink"), or all five: { "base": "#...", "light": "#...",
                                   "dark": "#...", "band": "#...", "low": "#..." }
      "colors": {                  any of the colours below; the ones not named keep the base palette's
        "screenBg": "#0b1d2a",
        "ink": "#e4f6ff"
      },
      "background": {              pictures behind the two screens (PNG or JPEG, scaled to fill: 1024 x 768 on
        "top": "mine/top.png",     the RG DS Plus, 640 x 480 on the RG DS), and the screens' dot pattern
        "bottom": "mine/bottom.png",
        "dither": false
      },
      "fonts": {                   your own fonts (TTF): the menu's text and its titles
        "regular": "mine/MyFont-Regular.ttf",
        "medium": "mine/MyFont-Medium.ttf"
      },
      "logo": "mine/logo.png",     the logo at the top of the bottom screen (fitted into its box)
      "icons": {                   a system's icon (PNG, about 128 x 128), by the system's id: nds, gba, gbc,
        "nds": "mine/ds.png",      gb, snes, nes, megadrive, n64, psx, pico-8, ports, ... (the folder names
        "gba": "mine/gba.png"      in roms/, and banana/systems.conf.default lists them all)
      },
      "sounds": {                  the menu's sounds (WAV): scroll, select, back, fav, ready, insert; "off"
        "scroll": "mine/tick.wav", for none (they change at the next start of the menu)
        "fav": "off"
      }
    }

File names are relative to the theme's .json (keep a theme's files in a folder of their own next to it), or full
paths. A file that isn't there, or doesn't load, keeps the default for that part only.

Colours are "#rgb", "#rrggbb" or "#rrggbbaa" (the last two digits: opacity, 00 clear to ff solid).
// and /* */ comments are allowed. A theme that can't be read shows Dark instead (the reason is in
banana/logs/banana-ui.log).

The colours, by what they paint
-------------------------------
The screen      screenBg (the background), screenBand (the band at the top), screenFrame (the screen's edge),
                ditherA, ditherB (the background's dot pattern), rule (thin lines)
Text            ink (text), muted (secondary text), line (dim lines and text), edge (bright outlines), navy
                (the brightest text), red (warnings)
Panels          panelBand, panelBody, panelLow (the big boxes: top band, body, bottom edge),
                chipBand, chipBody (small boxes and button chips), bubbleBand, bubbleBody, bubbleLow (the
                speech bubble over the shelf)
The shelf       sheetBase, sheetHi, sheetLo (the shelf's sheet), trackBg, trackHi, trackShade (the scroll
                track), railBg (the rail), slotOn (the selected slot), faceMuted (unselected face buttons),
                floor (shadows under the art), glint (the art's shine)
Box art         frameBorder, frameBg (the frame around the art), slotDark, slotLip (the console's slot),
                cartLabel (the label under a DS game's icon)
Favourites      favBorder, favBand, favBody, favInk
Other           meterBg (the battery and progress meters), brandBg (behind the logo), kbdBg, kbdInk (the
                on-screen keyboard's keys and letters), blackish, shade35, shade40, shade45 (shadows)
Settings        onPick (text on the selected row), onPickAccent (accented text on it), and the ones above that
                Settings uses too: ink, muted, line, edge, screen*, ditherA/B, panel*, bubble*, slotOn, kbd*

Themes apply to the Dark/Light style (ROCKNIXDS Pixel) and to Settings (its colours and fonts); the Carts theme
keeps its own look for now. theme-builder.html, next to this file, makes a theme in a web browser with a live
preview.

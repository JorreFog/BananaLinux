#!/usr/bin/env python3
"""The console the cartridge goes into, on the ready screen and in the boot animation: the back of a DSi-style
clamshell's lower half, seen from behind, drawn as pixel art in the theme's colours. The hinge barrel runs along
the top with the camera lens in its middle knuckle, L and R sit at its ends, the game card slot is centred in
the body under the hinge (its mouth is drawn by the UI, at the same place as before: 180 x 28 at x 230, 16 px
under the body's top), the stylus slot is on the right, the wrist strap's eyelet bottom-left, a screw in each
lower corner, the power light's bezel to the right of the slot.

Drawn at 1x on a 340 x 136 grid (CSS px of a 640 x 480 panel; the body's top is at y 26, under the hinge) and
scaled by whole numbers: x2 is the source, then cut to 1x and 1.6x like the rest of the theme's pictures.

    tools/make-console.py <out dir>        writes console-{dark,light}@{1x,1.6x}.png"""
import sys
from PIL import Image, ImageDraw

W, H = 340, 136
BODY_TOP = 26

THEMES = {
    # the theme's panel colours (RndsUI.cpp after tools/banana-palette.sh): band, body, low, edge, blackish, muted
    "dark": dict(band=(0x3a, 0x35, 0x40), body=(0x17, 0x16, 0x1c), low=(0x0b, 0x0a, 0x0d), edge=(0xe8, 0xe2, 0xd3),
                 black=(0x07, 0x06, 0x09), muted=(0xa3, 0x9d, 0x8e), hinge=(0x2a, 0x27, 0x2f), hingeHi=(0x4a, 0x45, 0x52),
                 lens=(0x10, 0x18, 0x28), lensHi=(0x7e, 0xa8, 0xd0), button=(0x2e, 0x2b, 0x33), buttonHi=(0x4a, 0x45, 0x52)),
    "light": dict(band=(0xff, 0xff, 0xff), body=(0xf6, 0xf2, 0xea), low=(0xd8, 0xd0, 0xc0), edge=(0x3a, 0x34, 0x2c),
                  black=(0x2a, 0x26, 0x20), muted=(0x7a, 0x72, 0x62), hinge=(0xe3, 0xdc, 0xcd), hingeHi=(0xff, 0xff, 0xff),
                  lens=(0x1d, 0x27, 0x3a), lensHi=(0x9e, 0xc8, 0xf0), button=(0xeb, 0xe5, 0xd9), buttonHi=(0xff, 0xff, 0xff)),
}


def rounded(d, box, r, fill, outline=None, width=0):
    d.rounded_rectangle(box, radius=r, fill=fill, outline=outline, width=width)


def console(t):
    im = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    E, B = t["edge"], t["body"]

    # the body: the lower shell's back, rounded, with the theme's band along the top and the low shadow at the bottom
    rounded(d, (0, BODY_TOP, W - 1, H - 1), 18, B, E, 3)
    d.rectangle((3, BODY_TOP + 3, W - 4, BODY_TOP + 10), fill=t["band"])
    d.rectangle((3, H - 9, W - 4, H - 4), fill=t["low"])
    # the band's corners follow the outline
    for x in (3, W - 4):
        d.point((x, BODY_TOP + 3), fill=E)

    # the hinge barrel: three knuckles across the top, the middle one with the camera lens
    hy0, hy1 = 0, BODY_TOP + 6
    for (x0, x1) in ((28, 120), (134, 206), (220, 312)):
        rounded(d, (x0, hy0, x1, hy1), 9, t["hinge"], E, 3)
        d.rectangle((x0 + 3, hy0 + 3, x1 - 3, hy0 + 6), fill=t["hingeHi"])      # the light along the top
        d.rectangle((x0 + 3, hy1 - 7, x1 - 3, hy1 - 3), fill=t["low"])          # the shadow under
    # the lens, in the middle knuckle
    cx, cy = 170, (hy0 + hy1) // 2
    d.ellipse((cx - 9, cy - 9, cx + 9, cy + 9), fill=E)
    d.ellipse((cx - 7, cy - 7, cx + 7, cy + 7), fill=t["lens"])
    d.rectangle((cx - 4, cy - 5, cx - 2, cy - 3), fill=t["lensHi"])
    # the microphone hole beside it
    d.rectangle((cx + 16, cy - 1, cx + 17, cy), fill=E)

    # L and R: the shoulder buttons at the hinge's ends, a little below it
    for (x0, x1, label) in ((2, 26, "L"), (W - 27, W - 3, "R")):
        rounded(d, (x0, 8, x1, BODY_TOP + 14), 6, t["button"], E, 3)
        d.rectangle((x0 + 3, 11, x1 - 3, 13), fill=t["buttonHi"])

    # the card slot's surround: a raised plate the UI's mouth sits in (the mouth itself: 180 x 28 at 230 - 150 = 80,
    # 16 under the body's top), with the label's embossed line under it
    sx0, sy0 = 80 - 8, BODY_TOP + 16 - 8
    rounded(d, (sx0, sy0, sx0 + 180 + 16 - 1, sy0 + 28 + 16 - 1), 8, t["band"], E, 3)
    d.rectangle((sx0 + 3, sy0 + 28 + 10, sx0 + 180 + 12, sy0 + 28 + 12), fill=t["low"])
    # the power light's bezel (the UI paints the light itself at 446 - 150 = 296, mouth + 8)
    d.rectangle((296 - 2, BODY_TOP + 16 + 8 - 2, 296 + 14 + 1, BODY_TOP + 16 + 8 + 10 + 1), fill=E)

    # the stylus, in its slot on the right: a long thin rounded bar with a grip line
    rounded(d, (W - 20, BODY_TOP + 44, W - 12, H - 24), 3, t["muted"], E, 2)
    d.rectangle((W - 18, BODY_TOP + 56, W - 14, BODY_TOP + 57), fill=E)

    # the wrist strap's eyelet, bottom-left; a screw in each lower corner
    rounded(d, (14, H - 34, 34, H - 20), 4, t["low"], E, 2)
    d.rectangle((20, H - 29, 28, H - 25), fill=t["black"])
    for x in (W - 30, 44):
        d.ellipse((x - 5, H - 27, x + 5, H - 17), fill=t["low"], outline=E, width=2)
        d.rectangle((x - 2, H - 22, x + 2, H - 22), fill=E)

    # the model line, embossed under the slot: three short dashes (no words)
    for i in range(3):
        d.rectangle((140 + i * 22, H - 46, 150 + i * 22, H - 45), fill=t["low"])
    return im


out = sys.argv[1]
for name, t in THEMES.items():
    im = console(t)
    im.save(f"{out}/console-{name}@1x.png")
    im.resize((int(W * 1.6), int(H * 1.6)), Image.NEAREST).save(f"{out}/console-{name}@1.6x.png")
print("console pictures written to", out)

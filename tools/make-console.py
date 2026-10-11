#!/usr/bin/env python3
"""The console the cartridge goes into, on the ready screen and in the boot animation: the back of a DSi XL's lower
half, drawn as pixel art in the theme's colours. A flat slab with the card slot at its top edge (SLOT-1; the UI
draws the mouth and the power light, 180 x 28 at x 230, 16 px under the slab's top), screws in the corners and
beside the slot, the battery cover on the right with its two screws, the charger jack top right, the stylus notch
and the volume slider on the left, the BANANAOS wordmark where the maker's is, the fine print under it, the
conformity marks, the maker's pill, the serial sticker.

Drawn at 1x on a 560 x 300 grid (CSS px of a 640 x 480 panel; the slab fills the bottom panel's width, the
screen cuts its lower part off) and scaled by whole numbers to 1.6x like the rest of the theme's pictures. The
slab sits at x 40 under the mouth, 26 px above it (the UI and the splash place it there).

    tools/make-console.py <rnds assets dir> <splash assets dir> <out dir>
        writes console-{dark,light}@{1x,1.6x}.png (logo@1x.png and word.png are read from the assets)"""
import sys
from PIL import Image, ImageDraw, ImageFont

W, H = 560, 300
MOUTH = (220, 26, 120, 28)       # the UI's slot mouth, in the slab (x, y, w, h): 260 - 40, 316 - 290

THEMES = {
    # the theme's colours (RndsUI.cpp after tools/banana-palette.sh): band, body, low, edge, blackish, muted, line
    "dark": dict(band=(0x3a, 0x35, 0x40), body=(0x17, 0x16, 0x1c), low=(0x0b, 0x0a, 0x0d), edge=(0xe8, 0xe2, 0xd3),
                 black=(0x07, 0x06, 0x09), muted=(0xa3, 0x9d, 0x8e), line=(0x8a, 0x83, 0x74), cover=(0x12, 0x11, 0x17),
                 paper=(0xf1, 0xed, 0xe2), screw=(0x2e, 0x2b, 0x33)),
    "light": dict(band=(0xff, 0xff, 0xff), body=(0xf6, 0xf2, 0xea), low=(0xd8, 0xd0, 0xc0), edge=(0x3a, 0x34, 0x2c),
                  black=(0x2a, 0x26, 0x20), muted=(0x7a, 0x72, 0x62), line=(0xa6, 0x9d, 0x8c), cover=(0xeb, 0xe5, 0xd9),
                  paper=(0xff, 0xff, 0xff), screw=(0xd6, 0xcf, 0xc0)),
}

GRID_SIZE = 1000 / 92           # Pixelify Sans's pixel grid, as tools/make-logo.py


def tiny(font, text):
    """a word in the theme's pixel font at one font pixel per image pixel, as a mask"""
    l, t, r, b = font.getbbox(text)
    im = Image.new("L", (int(r - l) + 4, int(b - t) + 4), 0)
    d = ImageDraw.Draw(im)
    d.fontmode = "1"
    d.text((2 - l, 2 - t), text, font=font, fill=255)
    return im.crop(im.getbbox())


def screw(d, x, y, t):
    d.ellipse((x - 6, y - 6, x + 6, y + 6), fill=t["screw"], outline=t["edge"], width=2)
    d.line((x - 3, y, x + 3, y), fill=t["edge"], width=2)
    d.line((x, y - 3, x, y + 3), fill=t["edge"], width=2)


def console(t, font, logo, word):
    im = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    E, B = t["edge"], t["body"]
    mx, my, mw, mh = MOUTH

    # the slab: the lower half's back, the hinge's rim as the theme's band along the top, the low shadow at the bottom
    d.rounded_rectangle((0, 0, W - 1, H - 1), radius=22, fill=B, outline=E, width=3)
    d.rectangle((3, 3, W - 4, 9), fill=t["band"])
    d.rectangle((3, H - 13, W - 4, H - 4), fill=t["low"])

    # the card slot: a dark recess the UI's mouth sits in, SLOT-1 over it, the card pictogram under it
    d.rounded_rectangle((mx - 6, my - 6, mx + mw + 5, my + mh + 5), radius=10, fill=t["low"], outline=E, width=2)
    lab = tiny(font, "SLOT-1")
    lab = lab.resize((lab.width * 2, lab.height * 2), Image.NEAREST)
    im.paste(Image.new("RGBA", lab.size, t["muted"] + (255,)), (mx + (mw - lab.width) // 2, my - 8 - lab.height), lab)
    cx, cy = mx + mw // 2, my + mh + 12
    d.rectangle((cx - 7, cy, cx + 7, cy + 10), fill=t["muted"])                 # the card
    d.rectangle((cx - 4, cy + 3, cx + 4, cy + 5), fill=B)
    d.polygon((cx - 5, cy + 14, cx + 5, cy + 14, cx, cy + 19), fill=t["muted"])  # the arrow: push to eject

    # the power light's bezel, right of the slot (the UI paints the light at mouth + 8, x 296)
    d.rectangle((406 - 2, my + 8 - 2, 406 + 14 + 1, my + 8 + 10 + 1), fill=E)
    # the charger jack, top right of it: a small socket with its label dots
    d.rectangle((W - 44, 18, W - 24, 28), fill=t["low"], outline=E, width=2)
    d.rectangle((W - 40, 22, W - 28, 24), fill=t["black"])

    # the battery cover: a slightly darker plate on the right with its two screws, its seam and the catch
    d.rounded_rectangle((W - 142, 48, W - 16, H - 22), radius=10, fill=t["cover"], outline=t["line"], width=2)
    d.rectangle((W - 142, 50, W - 141, H - 24), fill=t["low"])
    screw(d, W - 80, 76, t)
    screw(d, W - 80, H - 50, t)
    d.rectangle((W - 100, H - 32, W - 60, H - 29), fill=t["low"])

    # the screws: the corners, and one each side of the slot
    for x, y in ((22, 22), (W - 23, 22), (22, H - 23), (W - 23, H - 23), (mx - 26, my + 6), (mx + mw + 32, my + 6)):
        screw(d, x, y, t)

    # the stylus notch and the volume slider on the left edge
    d.rounded_rectangle((3, 66, 18, 116), radius=5, fill=t["low"], outline=E, width=2)
    d.rectangle((2, 134, 9, 166), fill=t["low"])
    d.rectangle((3, 144, 8, 150), fill=t["muted"])

    # the wordmark, where the maker's is: BANANAOS from the status bar's chip (the bunch and the word), centred
    lx = (W - logo.width) // 2
    ly = my + mh + 40
    im.alpha_composite(logo, (lx, ly))
    # the fine print under it: four rows of muted dashes, the last two shorter
    for i, (x0, x1) in enumerate(((130, 430), (134, 426), (170, 390), (220, 340))):
        y = ly + logo.height + 12 + i * 6
        x = x0
        while x < x1:
            d.rectangle((x, y, x + 8, y + 1), fill=t["line"])
            x += 12
    # the conformity marks: four little boxes in a row
    yb = ly + logo.height + 44
    for x in (207, 247, 287, 327):
        d.rectangle((x, yb, x + 26, yb + 15), outline=t["line"], width=2)
        d.rectangle((x + 7, yb + 5, x + 18, yb + 9), fill=t["line"])
    # the maker's pill, with the word
    px, py, pw, ph = 215, yb + 24, 130, 22
    d.rounded_rectangle((px, py, px + pw, py + ph), radius=11, fill=B, outline=t["muted"], width=2)
    word2 = word.resize((word.width * 2, word.height * 2), Image.NEAREST)
    im.paste(Image.new("RGBA", word2.size, t["muted"] + (255,)), (px + (pw - word2.width) // 2 + 1, py + (ph - word2.height) // 2 + 1), word2)
    # the serial sticker: paper with a barcode and a number line
    sx, sy = 195, yb + 56
    d.rectangle((sx, sy, sx + 170, sy + 22), fill=t["paper"])
    x = sx + 6
    for k, wbar in enumerate((2, 4, 2, 2, 6, 2, 4, 2, 2, 4, 6, 2, 2, 4, 2, 2, 6, 4, 2, 2, 4, 2, 2, 6, 2, 4, 2)):
        d.rectangle((x, sy + 3, x + wbar - 1, sy + 12), fill=(0x10, 0x10, 0x10))
        x += wbar + 3
    for k in range(10):
        d.rectangle((sx + 14 + k * 14, sy + 16, sx + 20 + k * 14, sy + 17), fill=(0x40, 0x40, 0x40))
    return im


rnds, splash, out = sys.argv[1], sys.argv[2], sys.argv[3]
font = ImageFont.truetype(f"{rnds}/fonts/PixelifySans-Medium.ttf", GRID_SIZE)
logo = Image.open(f"{rnds}/logo@1.6x.png").convert("RGBA")
logo = logo.crop(logo.getbbox())
word = Image.open(f"{splash}/word.png").convert("L")
for name, t in THEMES.items():
    im = console(t, font, logo, word)
    im.save(f"{out}/console-{name}@1x.png")
    im.resize((int(W * 1.6), int(H * 1.6)), Image.NEAREST).save(f"{out}/console-{name}@1.6x.png")
print("console pictures written to", out)

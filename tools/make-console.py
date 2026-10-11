#!/usr/bin/env python3
"""The console the cartridge goes into, on the ready screen and in the boot animation: the back of a DSi XL's lower
half, drawn as pixel art in the theme's colours, from a photo of the real one. The card slot is a notch in the
middle of the top edge (the UI draws the mouth over it: 120 x 28 at x 260, its top 6 px above the slab's edge, so
the card goes into the edge); under it SLOT-1 and the push-to-eject pictogram, a screw to the left of the slot and
the power light to the right; screws in the top corners, the battery cover on the right with its screw, the stylus
notch on the left edge, the BANANAOS wordmark where the maker's is and the fine print under it. The panel's bottom edge cuts the slab off
there.

Drawn per scale (1x for the RG DS, 1.6x for the RG DS Plus) on a 560 x 172 grid of CSS px, every coordinate scaled
and rounded, so both are crisp (no stretched pixels). The slab's top-left is at (40, 322) of the panel; the UI and
the splash place it there (the mouth at 260, 316; the power light's bezel at 409, 336).

    tools/make-console.py <rnds assets dir> <pixel banana dir> <out dir>
        writes console-{dark,light}@{1x,1.6x}.png"""
import sys
from PIL import Image, ImageDraw, ImageFont

W, H = 560, 172
SLOT = (220, -6, 120, 28)       # the UI's mouth in the slab (x, y, w, h): 260 - 40, 316 - 322
LED = (369, 14, 14, 10)         # the power light's bezel (x, y, w, h): 409 - 40, 336 - 322; the UI paints the light

THEMES = {
    # the theme's colours (RndsUI.cpp after tools/banana-palette.sh)
    "dark": dict(body=(0x17, 0x16, 0x1c), low=(0x0b, 0x0a, 0x0d), edge=(0xe8, 0xe2, 0xd3), muted=(0xa3, 0x9d, 0x8e),
                 line=(0x6a, 0x64, 0x58), cover=(0x1e, 0x1d, 0x24), screw=(0x2e, 0x2b, 0x33)),
    "light": dict(body=(0xf6, 0xf2, 0xea), low=(0xd8, 0xd0, 0xc0), edge=(0x3a, 0x34, 0x2c), muted=(0x7a, 0x72, 0x62),
                  line=(0xb4, 0xab, 0x9a), cover=(0xee, 0xe9, 0xde), screw=(0xd6, 0xcf, 0xc0)),
}
LIGHT_Y, DARK_Y, WHITE, SHADOW = (255, 222, 82, 255), (238, 160, 24, 255), (255, 255, 255, 255), (0, 0, 0, 255)
GRID_SIZE = 1000 / 92           # Pixelify Sans's pixel grid, as tools/make-logo.py


def text_mask(font, text, k):
    """a word in the theme's pixel font, k image px per font pixel (k whole), as a mask"""
    l, t, r, b = font.getbbox(text)
    im = Image.new("L", (int(r - l) + 4, int(b - t) + 4), 0)
    d = ImageDraw.Draw(im)
    d.fontmode = "1"
    d.text((2 - l, 2 - t), text, font=font, fill=255)
    im = im.crop(im.getbbox())
    return im.resize((im.width * k, im.height * k), Image.NEAREST)


def wordmark(font, bananas, k, banana_px):
    """BANANAOS as on the status bar's chip (tools/make-logo.py): the bunch, BANANA in two banana tones, OS white,
    a drop shadow; k image px per font pixel"""
    mask = text_mask(font, "BANANAOS", k)
    split = mask.width - text_mask(font, "OS", k).width
    ban = Image.open(f"{bananas}/banana-{banana_px}.png").convert("RGBA")
    gap = 3 * k
    sh = max(1, k // 2 + (1 if k > 2 else 0))
    out = Image.new("RGBA", (ban.width + gap + mask.width + sh, max(ban.height, mask.height + sh)), (0, 0, 0, 0))
    out.alpha_composite(ban, (0, (out.height - ban.height) // 2))
    tx, ty = ban.width + gap, (out.height - mask.height - sh) // 2
    fill = Image.new("RGBA", mask.size, DARK_Y)
    ImageDraw.Draw(fill).rectangle([0, 0, mask.width, mask.height * 11 // 20], fill=LIGHT_Y)
    ImageDraw.Draw(fill).rectangle([split, 0, mask.width, mask.height], fill=WHITE)
    out.paste(Image.new("RGBA", mask.size, SHADOW), (tx + sh, ty + sh), mask)
    out.paste(fill, (tx, ty), mask)
    return out


class Canvas:
    """drawing in CSS px on an image of scale s: every coordinate and width scaled and rounded"""
    def __init__(self, s):
        self.s = s
        self.im = Image.new("RGBA", (self.r(W), self.r(H)), (0, 0, 0, 0))
        self.d = ImageDraw.Draw(self.im)

    def r(self, v):
        return int(round(v * self.s))

    def box(self, x0, y0, x1, y1):      # [x0, x1) x [y0, y1) in CSS px -> PIL's inclusive box
        return (self.r(x0), self.r(y0), self.r(x1) - 1, self.r(y1) - 1)

    def rect(self, x0, y0, x1, y1, fill):
        if self.r(x1) > self.r(x0) and self.r(y1) > self.r(y0):
            self.d.rectangle(self.box(x0, y0, x1, y1), fill=fill)

    def rrect(self, x0, y0, x1, y1, radius, fill, outline=None, width=0):
        self.d.rounded_rectangle(self.box(x0, y0, x1, y1), radius=self.r(radius), fill=fill, outline=outline,
                                 width=max(1, self.r(width)) if outline else 0)

    def ellipse(self, x0, y0, x1, y1, fill, outline=None, width=0):
        self.d.ellipse(self.box(x0, y0, x1, y1), fill=fill, outline=outline, width=max(1, self.r(width)) if outline else 0)

    def poly(self, pts, fill):
        self.d.polygon([(self.r(x), self.r(y)) for x, y in pts], fill=fill)

    def mask(self, m, x, y, colour):    # a mask already at this scale, its top-left at CSS (x, y)
        self.im.paste(Image.new("RGBA", m.size, colour + (255,)), (self.r(x), self.r(y)), m)

    def picture(self, p, x, y):
        self.im.alpha_composite(p, (self.r(x), self.r(y)))


def screw(c, x, y, t):
    c.ellipse(x - 6, y - 6, x + 7, y + 7, t["screw"], t["edge"], 2)
    c.rect(x - 3, y, x + 4, y + 1.6, t["edge"])
    c.rect(x, y - 3, x + 1.6, y + 4, t["edge"])


def console(t, s, font, bananas):
    c = Canvas(s)
    E, B = t["edge"], t["body"]
    sx, sy, sw, sh = SLOT
    mid = sx + sw / 2                   # the slab's axis: 280, the panel's 320

    # the slab: the lower half's back, a 3 px outline, rounded top corners (the bottom is off the panel)
    c.rrect(0, 0, W, H + 30, 22, B, E, 3)
    # the slot's notch in the top edge: the outline steps down around the mouth, which the UI draws over it
    c.rrect(sx - 6, -10, sx + sw + 6, sy + sh + 6, 8, t["low"], E, 2)

    # under the slot: SLOT-1, then the card with its push-to-eject arrow, all on the axis
    lab = text_mask(font, "SLOT-1", c.r(2))
    ly = sy + sh + 12
    c.mask(lab, mid - lab.width / s / 2, ly, t["muted"])
    py = ly + lab.height / s + 5
    c.rect(mid - 7, py, mid + 7, py + 10, t["muted"])
    c.rect(mid - 4, py + 3, mid + 4, py + 5, B)
    c.poly([(mid - 5, py + 13), (mid + 5, py + 13), (mid, py + 18)], t["muted"])

    # a screw left of the slot and the power light's bezel right of it, the same distance off the axis
    lx, ly2, lw, lh = LED
    off = lx + lw / 2 - mid
    screw(c, mid - off, ly2 + lh / 2, t)
    c.rect(lx, ly2, lx + lw, ly2 + lh, E)
    # the screws in the top corners
    screw(c, 24, 22, t)
    screw(c, W - 24, 22, t)

    # the battery cover on the right: a plate with its seam, a screw
    c.rrect(W - 140, 44, W - 18, H + 20, 10, t["cover"], t["line"], 2)
    screw(c, W - 79, 70, t)
    # the stylus notch on the left edge, inside the outline
    c.rrect(6, 52, 20, 104, 5, t["low"], E, 2)

    # the wordmark where the maker's is, on the axis, then the fine print
    k = 3 if s < 1.3 else 5
    mark = wordmark(font, bananas, k, 32 if s < 1.3 else 48)
    wy = py + 30
    c.picture(mark, mid - mark.width / s / 2, wy)
    fy = wy + mark.height / s + 8
    for i, n in enumerate((19, 18, 14, 9)):    # dashes per row, each row centred on the axis
        y = fy + i * 6
        x = mid - (n * 12 - 4) / 2
        for _ in range(n):
            c.rect(x, y, x + 8, y + 2, t["line"])
            x += 12
    return c.im


rnds, bananas, out = sys.argv[1], sys.argv[2], sys.argv[3]
font = ImageFont.truetype(f"{rnds}/fonts/PixelifySans-Medium.ttf", GRID_SIZE)
for name, t in THEMES.items():
    for s, sfx in ((1.0, "1x"), (1.6, "1.6x")):
        console(t, s, font, bananas).save(f"{out}/console-{name}@{sfx}.png")
print("console pictures written to", out)

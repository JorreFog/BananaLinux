#!/usr/bin/env python3
"""Banana's status-bar logo, in the place and style of ROCKNIXDS Pixel's: the word in the Unbounded font (light),
then the RG DS's two screens as a small stacked glyph, at 1x (134x20, the RG DS) and 1.6x (214x32, the RG DS Plus).
Drawn 8x larger and filtered down, so its edges are anti-aliased the way a browser draws the original SVG.

    tools/make-logo.py <Unbounded-VF.ttf> <out dir> [big.png]   (writes logo@1x.png and logo@1.6x.png)

The font is in ROCKNIXDS's logo/ folder (SIL Open Font License)."""
import sys
from PIL import Image, ImageDraw, ImageFont

YELLOW = (245, 197, 24, 255)
GREY = (138, 138, 138, 255)
W1, H1 = 134, 20
SS = 8


def draw(scale):
    w, h = round(W1 * scale), round(H1 * scale)
    big = Image.new("RGBA", (w * SS, h * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(big)
    font = ImageFont.truetype(sys.argv[1], int(h * SS * 0.86))
    try:
        font.set_variation_by_axes([300])
    except Exception:
        pass
    # "BANA" yellow, "NA" grey, as ROCKNIX is red and grey
    parts = [("BANA", YELLOW), ("NA", GREY)]
    asc = font.getbbox("BANANA")
    text_w = sum(font.getlength(t) for t, _ in parts)
    glyph_w = h * SS * 0.62
    gap = h * SS * 0.28
    x = (w * SS - (text_w + gap + glyph_w)) / 2
    y = (h * SS - (asc[3] - asc[1])) / 2 - asc[1]
    for t, c in parts:
        d.text((x, y), t, font=font, fill=c)
        x += font.getlength(t)
    # the two screens: two rounded outlines stacked, the top one yellow
    gx = x + gap
    sw = max(SS, round(h * SS * 0.09))
    gh = (h * SS - sw) / 2
    for i, c in enumerate((YELLOW, GREY)):
        top = i * (gh + sw * 0.5)
        d.rounded_rectangle([gx, top + sw * 0.5, gx + glyph_w, top + gh], radius=sw * 1.2, outline=c, width=sw)
    return big.resize((w, h), Image.LANCZOS)


out = sys.argv[2]
draw(1).save(out + "/logo@1x.png")
draw(1.6).save(out + "/logo@1.6x.png")
if len(sys.argv) > 3:
    draw(8).save(sys.argv[3])     # a large one for the splash (tools/make-splash.mjs)

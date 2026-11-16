#!/usr/bin/env python3
"""Draw Randomizer's 32x32 shelf icon: a little die, in the icon pack's pixel style.

    python tools/make-icon.py ../../ui/assets/rnds/icons/randomizer.png
"""
import os
import sys

from PIL import Image, ImageDraw

OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "randomizer.png")

BLACK = (0x17, 0x16, 0x1c, 255)
CREAM = (0xf1, 0xed, 0xe2, 255)
LIGHT = (0xff, 0xfa, 0xf0, 255)
SHADE = (0xc9, 0xbf, 0xa8, 255)
ACCENT = (0xe8, 0xa3, 0x3d, 255)
ACCENT_L = (0xff, 0xcf, 0x5c, 255)
ACCENT_D = (0xb8, 0x76, 0x18, 255)

im = Image.new("RGBA", (32, 32), (0, 0, 0, 0))
d = ImageDraw.Draw(im)

# the shadow: two hard steps, as the icon pack draws them
d.rounded_rectangle([8, 10, 27, 29], radius=5, fill=(0x0b, 0x0a, 0x0d, 120))
d.rounded_rectangle([7, 9, 26, 28], radius=5, fill=(0x0b, 0x0a, 0x0d, 160))

# the die: black edge, a cream face with a lighter top and a shaded bottom lip
d.rounded_rectangle([5, 5, 26, 26], radius=5, fill=BLACK)
d.rounded_rectangle([6, 6, 25, 25], radius=4, fill=CREAM)
d.rectangle([7, 7, 24, 8], fill=LIGHT)
d.rectangle([7, 23, 24, 24], fill=SHADE)

# the five pips, each 2x2 with a black rim
for x, y in ((10, 10), (20, 10), (15, 15), (10, 20), (20, 20)):
    d.rectangle([x - 1, y - 1, x + 3, y + 3], fill=BLACK)
    d.rectangle([x, y, x + 2, y + 2], fill=(0x3a, 0x35, 0x40, 255))
    d.rectangle([x, y, x + 1, y + 1], fill=(0x17, 0x16, 0x1c, 255))

# the accent: the top-left corner of the face, and a glint
d.polygon([(6, 6), (6, 12), (12, 6)], fill=ACCENT)
d.polygon([(6, 6), (6, 9), (9, 6)], fill=ACCENT_L)
d.polygon([(7, 7), (7, 10), (10, 7)], fill=ACCENT_D)
d.line([14, 8, 20, 8], fill=LIGHT)

im.save(OUT)
print("wrote", OUT)

#!/usr/bin/env python3
"""The shelf's Files tile: a folder as pixel art, in the style of ROCKNIXDS Pixel's 32x32 icons (BenJelter's console
icons): a black 1 px outline, a few flat tones lit from the top left, no anti-aliasing. The folder's back with its
tab behind, its front flap over it, a highlight along the flap's top edge and a shadow along its bottom.

    tools/make-files-icon.py <out.png>      (ui/assets/overrides/icons/files.png; import-rnds.sh copies it into rnds/)

Scale the result by whole multiples only."""
import sys
from PIL import Image

OUTLINE = (0, 0, 0, 255)
HIGH, BASE, SHADE, DEEP = (255, 228, 140, 255), (242, 188, 66, 255), (214, 146, 40, 255), (160, 98, 24, 255)
W = 32


def main(out):
    px = [[None] * W for _ in range(W)]

    def rect(x0, y0, x1, y1, c):
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                px[y][x] = c

    rect(5, 6, 12, 8, SHADE)        # the tab
    rect(5, 8, 26, 25, SHADE)       # the back
    rect(5, 12, 26, 25, BASE)       # the front flap
    rect(6, 12, 25, 12, HIGH)       # its top edge, lit
    rect(5, 13, 5, 24, HIGH)        # and its left one
    rect(6, 24, 26, 25, DEEP)       # its bottom, in shadow
    rect(13, 7, 13, 8, SHADE)       # the tab's slope down to the back
    px[6][13] = None
    # the outline: every empty pixel next to the folder (not at a corner: the edges round off), and the flap's top
    shape = [[c is not None for c in row] for row in px]
    for y in range(W):
        for x in range(W):
            if shape[y][x]:
                continue
            if any(0 <= y + dy < W and 0 <= x + dx < W and shape[y + dy][x + dx] for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))):
                px[y][x] = OUTLINE
    rect(5, 11, 26, 11, OUTLINE)
    img = Image.new("RGBA", (W, W), (0, 0, 0, 0))
    for y in range(W):
        for x in range(W):
            if px[y][x]:
                img.putpixel((x, y), px[y][x])
    img.save(out)


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "ui/assets/overrides/icons/files.png")

#!/usr/bin/env python3
"""Banana's mark as pixel art, in the style of ROCKNIXDS Pixel's 32x32 icons (BenJelter's console icons): a black
1 px outline, a few flat tones lit from the top left, no anti-aliasing.

It redraws the BananaUI mark (JorreFog/helios, app/src/main/res/drawable-nodpi/banana_mark.png: a bunch of three
bananas hanging from a stem at the top right, their tips fanning out to the bottom left) pixel by pixel: each
banana is a tapered curve from the stem to its tip, bulging to the bottom right. A pixel belongs to the frontmost
banana that covers it; where a banana lies in front of another, the one behind gets a black seam, so the three
read apart even at 16 px. Across each banana the shade steps from a pale highlight on its top-left edge to a
deep shadow on its bottom-right one, as the original's lighting does.

    tools/make-pixel-banana.py <out dir>      writes banana-<N>.png for N = 16, 20, 24, 32, 48

Scale the results by whole multiples only."""
import math
import sys
from PIL import Image

OUTLINE = (0, 0, 0, 255)
# across a banana, top left to bottom right
TONES = [(255, 244, 170, 255), (255, 214, 64, 255), (246, 178, 28, 255), (214, 132, 18, 255), (150, 82, 16, 255)]
STEM = [(120, 76, 30, 255), (78, 46, 18, 255)]
TIP = (92, 56, 22, 255)

# in a unit square (0,0 top left): the stem's end, and each banana as (tip, bulge control point, half-width at its
# widest), back to front. The tips fan from the left to the bottom, as in the mark.
STEM_AT = (0.70, 0.07)
BANANAS = [
    ((0.07, 0.56), (0.60, 0.60), 0.105),   # back: the upper one, tip to the left
    ((0.13, 0.76), (0.72, 0.78), 0.110),   # middle
    ((0.40, 0.90), (0.92, 0.88), 0.110),   # front: the lower one, tip to the bottom
]


def bezier(p0, p1, p2, t):
    u = 1 - t
    return (u * u * p0[0] + 2 * u * t * p1[0] + t * t * p2[0], u * u * p0[1] + 2 * u * t * p1[1] + t * t * p2[1])


def curve(tip, ctrl, steps=200):
    """points along the banana from the stem (t=0) to its tip (t=1)"""
    start = (STEM_AT[0], STEM_AT[1] + 0.08)
    return [bezier(start, ctrl, tip, i / steps) for i in range(steps + 1)]


def halfwidth(t, w):
    # thin at the stem, widest just past the middle, rounding off at the tip
    return w * (0.35 + 0.65 * math.sin(math.pi * min(1.0, t * 0.92 + 0.04)) ** 0.8)


def draw(n):
    pts = [curve(tip, ctrl) for tip, ctrl, _ in BANANAS]
    owner = [[-1] * n for _ in range(n)]
    across = [[0.0] * n for _ in range(n)]
    along = [[0.0] * n for _ in range(n)]
    for y in range(n):
        for x in range(n):
            px, py = (x + 0.5) / n, (y + 0.5) / n
            for i, (tip, ctrl, w) in enumerate(BANANAS):
                c = pts[i]
                best, bi = 9.0, 0
                for k, (cx, cy) in enumerate(c):
                    d = (px - cx) ** 2 + (py - cy) ** 2
                    if d < best:
                        best, bi = d, k
                t = bi / (len(c) - 1)
                hw = halfwidth(t, w)
                d = math.sqrt(best)
                if d <= hw:
                    # which side of the curve: towards the top left is negative
                    k0, k1 = max(0, bi - 1), min(len(c) - 1, bi + 1)
                    tx, ty = c[k1][0] - c[k0][0], c[k1][1] - c[k0][1]
                    side = tx * (py - c[bi][1]) - ty * (px - c[bi][0])
                    owner[y][x] = i	# later (front) bananas win
                    across[y][x] = (d / hw) * (1 if side < 0 else -1)
                    along[y][x] = t
    img = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    p = img.load()
    for y in range(n):
        for x in range(n):
            o = owner[y][x]
            if o < 0:
                continue
            # a seam: this banana is behind a neighbour's
            if any(0 <= y + dy < n and 0 <= x + dx < n and owner[y + dy][x + dx] > o for dx, dy in ((1, 0), (0, 1), (-1, 0), (0, -1))):
                p[x, y] = OUTLINE
                continue
            a = across[y][x]          # -1 top-left edge .. +1 bottom-right edge
            band = 0 if a < -0.62 else 1 if a < -0.05 else 2 if a < 0.45 else 3 if a < 0.8 else 4
            p[x, y] = TONES[band]
            if along[y][x] > 0.965:
                p[x, y] = TIP
    # the stem: a short stalk above where the bananas meet
    sx, sy = int(STEM_AT[0] * n), int(STEM_AT[1] * n)
    sw = max(1, round(n / 14))
    for yy in range(sy, sy + max(2, round(n * 0.10))):
        for xx in range(sx - sw // 2, sx - sw // 2 + sw + 1):
            if 0 <= xx < n and 0 <= yy < n and (owner[yy][xx] < 0 or yy < sy + 2):
                p[xx, yy] = STEM[0] if xx <= sx else STEM[1]
                owner[yy][xx] = 9
    # the outline around everything
    filled = [[p[x, y][3] > 0 for x in range(n)] for y in range(n)]
    for y in range(n):
        for x in range(n):
            if not filled[y][x] and any(0 <= y + dy < n and 0 <= x + dx < n and filled[y + dy][x + dx] for dx, dy in ((1, 0), (0, 1), (-1, 0), (0, -1))):
                p[x, y] = OUTLINE
    return img


out = sys.argv[1]
for n in (16, 20, 24, 32, 48):
    draw(n).save(f"{out}/banana-{n}.png")

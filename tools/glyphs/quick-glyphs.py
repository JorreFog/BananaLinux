#!/usr/bin/env python3
# The quick menu's own line icons for the Thor themes (white on clear, tinted when drawn), in the two sizes the
# theme's other glyphs have:  python3 tools/glyphs/quick-glyphs.py system/rootfs/usr/share/banana/themes/thor-oled/ui
import math, sys
from PIL import Image, ImageDraw

SS = 8  # drawn this many times larger, then scaled down

def canvas():
    im = Image.new("L", (32 * SS, 32 * SS), 0)
    return im, ImageDraw.Draw(im)

def P(v): return v * SS
def line(d, pts, w=2.5):
    d.line([(P(x), P(y)) for x, y in pts], fill=255, width=int(P(w)), joint="curve")
    r = P(w) / 2
    for x, y in (pts[0], pts[-1]): d.ellipse([P(x) - r, P(y) - r, P(x) + r, P(y) + r], fill=255)
def rrect(d, x0, y0, x1, y1, r, w=2.5, fill=False):
    d.rounded_rectangle([P(x0), P(y0), P(x1), P(y1)], radius=P(r), outline=255, width=int(P(w)), fill=255 if fill else None)

def chip(d):
    rrect(d, 8, 8, 24, 24, 3)
    rrect(d, 13, 13, 19, 19, 1, fill=True)
    for v in (12, 16, 20):
        line(d, [(v, 4), (v, 6)]); line(d, [(v, 26), (v, 28)]); line(d, [(4, v), (6, v)]); line(d, [(26, v), (28, v)])

def gpu(d):
    rrect(d, 4, 8, 28, 22, 3)
    d.ellipse([P(8), P(11), P(16), P(19)], outline=255, width=int(P(2.5)))
    line(d, [(20, 13), (24, 13)]); line(d, [(20, 17), (24, 17)])
    line(d, [(9, 26), (9, 24)]); line(d, [(14, 26), (14, 24)]); line(d, [(19, 26), (19, 24)])

def memory(d):
    for y in (5, 13.5, 22):
        rrect(d, 5, y, 27, y + 5.5, 2, fill=True)
        d.ellipse([P(8), P(y + 1.75), P(10), P(y + 3.75)], fill=0)
        d.rectangle([P(13), P(y + 2.25), P(23), P(y + 3.25)], fill=0)

def fan(d):
    cx = cy = 16
    for k in range(3):
        a = math.radians(k * 120 - 90)
        pts = []
        for t in range(0, 101, 5):
            # a blade: a leaf from the hub outwards, leaning the way it turns
            u = t / 100
            r = 3 + 9 * u
            wdt = 6 * math.sin(math.pi * u)
            b = a + 0.5 * u
            pts.append((cx + r * math.cos(b) - wdt * math.sin(b), cy + r * math.sin(b) + wdt * math.cos(b)))
        for t in range(100, -1, -5):
            u = t / 100
            r = 3 + 9 * u
            b = a + 0.5 * u
            pts.append((cx + r * math.cos(b), cy + r * math.sin(b)))
        d.polygon([(P(x), P(y)) for x, y in pts], fill=255)
    d.ellipse([P(13.2), P(13.2), P(18.8), P(18.8)], fill=255)

def ring(d):
    d.ellipse([P(6), P(6), P(26), P(26)], outline=255, width=int(P(4.5)))
    d.ellipse([P(13.5), P(13.5), P(18.5), P(18.5)], fill=255)

def battery(d):
    rrect(d, 4, 10, 25, 22, 3)
    rrect(d, 27, 13.5, 28.5, 18.5, 0.7, fill=True)
    rrect(d, 7.5, 13.5, 21.5, 18.5, 1, fill=True)

def rocket(d):
    # the body, nose up and to the right; a window; a fin each side; the flame
    d.polygon([(P(x), P(y)) for x, y in [(26, 6), (25, 14), (17, 22), (10, 15), (18, 7)]], fill=255)
    d.ellipse([P(22), P(4.5), P(27.5), P(10)], fill=255)
    d.polygon([(P(x), P(y)) for x, y in [(11, 13), (6, 14), (9, 17.5)]], fill=255)
    d.polygon([(P(x), P(y)) for x, y in [(19, 21), (18, 26), (14.5, 23)]], fill=255)
    line(d, [(9, 20), (6, 23)]); line(d, [(12, 23), (8, 27)], 2)
    d.ellipse([P(17.5), P(10.5), P(21.5), P(14.5)], fill=0)

out = sys.argv[1]
for name, fn in (("chip", chip), ("gpu", gpu), ("memory", memory), ("fan", fan), ("ring", ring), ("battery", battery), ("rocket", rocket)):
    im, d = canvas()
    fn(d)
    for size in (32, 88):
        a = im.resize((size, size), Image.LANCZOS)
        rgba = Image.merge("RGBA", (Image.new("L", a.size, 255),) * 3 + (a,))
        rgba.save(f"{out}/{name}-{size}.png", optimize=True)

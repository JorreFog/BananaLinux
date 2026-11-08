#!/usr/bin/env python3
# Glass themes: the frosted panels baked into the screens' pictures. The panels never move, so the blur is done here
# once instead of on the handheld every frame: the picture under each panel blurred, tinted, with a hairline edge and
# a soft shadow. Hints' capsules move with their labels, so the band under them is blurred (fading in) and the theme
# tints them itself.
#
#   python3 tools/glass-theme/bake.py      (needs Pillow; writes system/rootfs/usr/share/banana/themes/glass/*.jpg)
#
# Your own picture: put it in tools/glass-theme/ as <variant>-top.jpg / <variant>-bottom.jpg (1024 x 768) and run it.
# The panels' places are the theme's (glass-dark.json), in its 640 x 480 units.
import os
from PIL import Image, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "../../system/rootfs/usr/share/banana/themes/glass")
K = 1024 / 640                  # the pictures' pixels a unit
BLUR = 14 * K                   # as iOS's material

VARIANTS = {
    # tint (rgba), edge (rgba), shadow alpha
    "dark": ((20, 22, 30, 107), (255, 255, 255, 48), 89),
    "light": ((255, 255, 255, 77), (255, 255, 255, 128), 51),
}

# the status corners, on every top screen: the date and time, Wi-Fi and the battery
STATUS = [(15, 11, 170, 35, 17.5), (517, 11, 108, 35, 17.5)]
VIEWS = {
    ("home", "top"): STATUS + [(132, 75, 375, 300, 28)],
    ("home", "bottom"): [(15, 11, 610, 40, 20, False), (25, 156, 590, 138, 32)],
    ("library", "top"): STATUS + [(40, 69, 560, 270, 28)],
    ("library", "bottom"): [(15, 11, 610, 40, 20, False), (25, 125, 590, 200, 32)],
}
BAND = (406, 474)               # the hints' row on the bottom screens (units): blurred, fading in and out


def mask(size, box, r):
    m = Image.new("L", size, 0)
    x, y, w, h = (v * K for v in box)
    ImageDraw.Draw(m).rounded_rectangle((x, y, x + w - 1, y + h - 1), r * K, fill=255)
    return m


def bake(src, panes, tint, edge, shade, band):
    img = Image.open(src).convert("RGB").resize((1024, 768), Image.LANCZOS)
    blurred = img.filter(ImageFilter.GaussianBlur(BLUR))
    out = img.convert("RGBA")
    if band:
        # the hints' band: blurred, its edges fading over 12 units
        m = Image.new("L", out.size, 0)
        d = ImageDraw.Draw(m)
        top, bot, fade = band[0] * K, band[1] * K, 12 * K
        for y in range(out.size[1]):
            a = min(1, max(0, (y - (top - fade)) / fade), max(0, ((bot + fade) - y) / fade))
            d.line((0, y, out.size[0], y), fill=int(255 * a))
        out.paste(blurred, (0, 0), m)
    for p in panes:
        x, y, w, h, r = p[:5]
        shadow = p[5] if len(p) > 5 else True
        if shadow:
            s = mask(out.size, (x, y + 5, w, h), r).filter(ImageFilter.GaussianBlur(11 * K / 2))
            out.paste(Image.new("RGBA", out.size, (0, 0, 0, 255)), (0, 0), s.point(lambda v: v * shade // 255))
        m = mask(out.size, (x, y, w, h), r)
        out.paste(blurred, (0, 0), m)
        out.paste(Image.new("RGBA", out.size, tint), (0, 0), m.point(lambda v: v * tint[3] // 255))
        # the hairline: 1 unit inside the edge
        ring = Image.new("L", out.size, 0)
        X, Y, W, H = (v * K for v in (x, y, w, h))
        ImageDraw.Draw(ring).rounded_rectangle((X, Y, X + W - 1, Y + H - 1), r * K, outline=255, width=round(K))
        out.paste(Image.new("RGBA", out.size, edge), (0, 0), ring.point(lambda v: v * edge[3] // 255))
    return out.convert("RGB")


for variant, (tint, edge, shade) in VARIANTS.items():
    for (view, side), panes in VIEWS.items():
        src = os.path.join(HERE, variant + "-" + side + ".jpg")
        dst = os.path.join(OUT, variant + "-" + view + "-" + side + ".jpg")
        bake(src, panes, tint, edge, shade, BAND if side == "bottom" else None).save(dst, quality=88, optimize=True)
        print(os.path.relpath(dst, os.path.join(HERE, "../..")))

# Glass light: Glass dark's file with white glass and dark ink
THEMES = os.path.join(OUT, "..")
s = open(os.path.join(THEMES, "glass-dark.json"), encoding="utf-8").read()
for a, b in [
    ("// Glass dark: frosted panels over a still picture, iPhone-like icons (Canvas's consoles, CC0). Glass light is the\n"
     "  // same with white glass.",
     "// Glass light: frosted panels over a still picture, iPhone-like icons (Canvas's consoles, CC0). Glass dark is the\n"
     "  // same with smoked glass. Made from glass-dark.json by tools/glass-theme/bake.py: change that one."),
    ('"name": "Glass dark"', '"name": "Glass light"'),
    ('"base": "dark"', '"base": "light"'),
    ('"screenBg": "#0a0c18", "ink": "#ffffff", "muted": "#b8bccc"', '"screenBg": "#f2eef6", "ink": "#14141c", "muted": "#5a5a68"'),
    ('"pics": "glass/dark-"', '"pics": "glass/light-"'),
    ('"base": "#0a0c18"', '"base": "#f2eef6"'),
    ('"ink": "#ffffff",', '"ink": "#14141c",'),
    ('"soft": "#ffffffa0"', '"soft": "#14141c96"'),
    ('"pane": "#14161e8c",        // a hint\'s capsule', '"pane": "#ffffff73",        // a hint\'s capsule'),
    ('"paneHi": "#ffffff38"', '"paneHi": "#ffffff8c"'),
    ('"edge": "#ffffff30"', '"edge": "#ffffff80"'),
    ('"chip": "#ffffff"', '"chip": "#14141c"'),
    ('"chipInk": "#1e1e24"', '"chipInk": "#ffffff"'),
    ('"shade": "#00000059"', '"shade": "#00000033"'),
]:
    assert s.count(a) == 1, a
    s = s.replace(a, b)
open(os.path.join(THEMES, "glass-light.json"), "w", encoding="utf-8").write(s)
print("system/rootfs/usr/share/banana/themes/glass-light.json")

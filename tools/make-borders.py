#!/usr/bin/env python3
"""Screen borders for the systems RetroArch runs (Settings > Other systems > Screen border): the black area around a
game's picture filled in banana-ui's own look (the menu screen's warm charcoal or cream with its dot grid, its dark
edge and top band, a recessed well around the picture with a rim in the system's shelf colour, the system's name in
Pixelify Sans and DS-like speaker grilles where there is room). Nothing is drawn over the picture: the hole is clear.

    tools/make-borders.py [<out dir>]      (default: system/rootfs/usr/share/banana/borders)

Writes, per panel mode (1024x768: the RG DS Plus, 640x480: the RG DS) and theme (dark, light and the image's example
themes): <mode>/<theme>/<system>.png and <system>.cfg (a RetroArch overlay: the picture as one full-screen image), and
one borders.conf: `<mode> <system> <x> <y> <w> <h>`, the picture's rectangle, which run-retroarch gives RetroArch as its
custom viewport so the game is drawn exactly in the hole.

Where the picture goes, per system: its whole-number size (k x its native height, k as large as fits) when that is at
least 80% of the size that fills the panel, else the size that fills it (aspect kept); margins even on both sides. A
system whose picture fills the panel either way (N64 and PS1 on the RG DS: 640x480 at 4:3) gets no border there and
is left out of borders.conf (the setting does nothing for it). Run again after changing anything here; the output is
the same for the same input (PNG written without timestamps)."""
import json
import os
import re
import sys
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
ASSETS = os.path.join(ROOT, "ui", "assets", "rnds")
FONT = os.path.join(ASSETS, "fonts", "PixelifySans-Medium.ttf")
ICONS = os.path.join(ASSETS, "icons")
THEMES = os.path.join(ROOT, "system", "rootfs", "usr", "share", "banana", "themes")
GRID = 1000 / 92    # Pixelify Sans: at this size one font pixel is one image pixel (tools/make-readme-art.py)

# the panels by their mode (the RG DS Plus, the RG DS): width, height, banana-ui's scale
PANELS = {"1024x768": (1024, 768, 1.6), "640x480": (640, 480, 1.0)}

# systems.conf ids -> (name on the border, icon in rnds/icons, shelf accent from rnds/systems.cfg, picture aspect,
# native lines). The aspect is the one RetroArch shows with the core's own ("core provided"), measured on the
# handhelds with Banana's core options: fceumm 8:7 PAR at 240 lines, Picodrive 320x224 at 4:3... see SYSTEMS_NOTE.
SYSTEMS = {
    "gb":        ("GAME BOY",          "gb.png",   "#8a9144", 10 / 9, 144),
    "gbc":       ("GAME BOY COLOR",    "gbc.png",  "#2eae62", 10 / 9, 144),
    "gba":       ("GAME BOY ADVANCE",  "gba.png",  "#8b6ad8", 3 / 2, 160),
    "nes":       ("NES",               "nes.png",  "#e25a5a", 256 * 8 / 7 / 240, 240),
    "snes":      ("SUPER NINTENDO",    "snes.png", "#a07ad4", 4 / 3, 224),
    "megadrive": ("MEGA DRIVE",        "md.png",   "#2eabc8", 4 / 3, 224),
    "psx":       ("PLAYSTATION",       "psx.png",  "#5c86e0", 4 / 3, 240),
    "n64":       ("NINTENDO 64",       "n64.png",  "#6d8a58", 4 / 3, 240),
    "pico-8":    ("PICO-8",            "fantasy.png", "#e25a5a", 1.0, 128),
}

# banana-ui's two palettes (ui/rnds/RndsUI.cpp darkPal/lightPal), the colours a border uses
BASE = {
    "dark": {"screenBg": "#17161c", "screenBand": "#2e2b33", "screenFrame": "#0b0a0d", "muted": "#a39d8e",
             "rule": "#45403d", "well": "#070609", "dotA": (255, 255, 255, 0.14), "dotB": (255, 255, 255, 0.07),
             "hole": (0, 0, 0, 0.45), "label": 0.55},
    "light": {"screenBg": "#f3efe6", "screenBand": "#fdfcf9", "screenFrame": "#d4ccbd", "muted": "#7a7262",
              "rule": "#d6cfc0", "well": "#2a2620", "dotA": (0x3a, 0x2e, 0x14, 0.07), "dotB": (0x3a, 0x2e, 0x14, 0.035),
              "hole": (0x3a, 0x2e, 0x14, 0.22), "label": 0.7},
}


def hexrgb(s):
    s = s.lstrip("#")
    if len(s) == 3:
        s = "".join(c * 2 for c in s)
    return tuple(int(s[i:i + 2], 16) for i in (0, 2, 4))


def mix(a, b, t):
    """a toward b by t (0: a, 1: b)"""
    return tuple(round(x + (y - x) * t) for x, y in zip(a, b))


def theme_palettes():
    """dark, light, and every example theme in the image (its base palette with the colours it names)"""
    out = {"dark": dict(BASE["dark"]), "light": dict(BASE["light"])}
    for f in sorted(os.listdir(THEMES)):
        if not f.endswith(".json"):
            continue
        txt = open(os.path.join(THEMES, f), encoding="utf-8").read()
        txt = re.sub(r"/\*.*?\*/", "", txt, flags=re.S)
        txt = re.sub(r'^\s*//.*$|(?<=[,{\[\s])//[^"\n]*$', "", txt, flags=re.M)
        try:
            j = json.loads(txt)
        except json.JSONDecodeError as e:
            sys.exit(f"{f}: {e}")
        p = dict(BASE["light" if j.get("base") == "light" else "dark"])
        for k, v in (j.get("colors") or {}).items():
            if k in p and isinstance(v, str):
                p[k] = v[:7]
        out[f[:-5]] = p
    return out


def rect_for(system, W, H):
    _, _, _, aspect, lines = SYSTEMS[system]
    fit_h = min(H, W / aspect)
    k = int(H // lines)
    h = k * lines
    if h < 0.8 * fit_h or round(h * aspect) > W:
        if W / aspect <= H:
            w, h = W, round(W / aspect)        # fills the width
        else:
            w, h = round(H * aspect), H
    else:
        w = round(h * aspect)
    if (W - w) % 2:
        w -= 1
    if (H - h) % 2:
        h -= 1
    return (W - w) // 2, (H - h) // 2, w, h


def spaced(t, s, gap):
    """letter-spaced text (like the menu's START): one glyph at a time on a common baseline, `gap` pixels between"""
    f = ImageFont.truetype(FONT, GRID * s)
    w = int(sum(f.getlength(c) for c in t) + gap * len(t)) + 8 * s
    im = Image.new("L", (w, int(GRID * s * 2)), 0)
    d = ImageDraw.Draw(im)
    d.fontmode = "1"
    x = 4 * s
    for c in t:
        d.text((x, 4 * s), c, font=f, fill=255)
        x += f.getlength(c) + gap
    return im.crop(im.getbbox())


def blend(img, box, rgba):
    """a translucent rectangle over img (RGBA, opaque background)"""
    x0, y0, x1, y1 = [int(v) for v in box]
    if x1 <= x0 or y1 <= y0:
        return
    r, g, b, a = rgba
    layer = Image.new("RGBA", (x1 - x0, y1 - y0), (r, g, b, round(a * 255)))
    img.alpha_composite(layer, (x0, y0))


def pixel_rrect(d, box, r, fill):
    """a rectangle with stepped (pixel-art) corners of radius r"""
    x0, y0, x1, y1 = box
    if r <= 0:
        d.rectangle((x0, y0, x1 - 1, y1 - 1), fill=fill)
        return
    steps = [r - round((r * r - (r - i - 0.5) ** 2) ** 0.5) for i in range(r)]
    d.rectangle((x0, y0 + r, x1 - 1, y1 - 1 - r), fill=fill)
    for i, inset in enumerate(steps):
        d.line((x0 + inset, y0 + i, x1 - 1 - inset, y0 + i), fill=fill)
        d.line((x0 + inset, y1 - 1 - i, x1 - 1 - inset, y1 - 1 - i), fill=fill)


def grille(img, cx, cy, cols, rows, pitch, dot, hole, hi):
    """a DS-style speaker grille: staggered rows of small round holes, each dark with a highlight on its lower lip"""
    x0 = cx - (cols - 1) * pitch // 2
    y0 = cy - (rows - 1) * pitch // 2
    d = ImageDraw.Draw(img)
    cut = 1 if dot >= 4 else 0
    for j in range(rows):
        for i in range(cols):
            if (i + j) % 2:
                continue
            x, y = x0 + i * pitch, y0 + j * pitch
            d.line((x + cut, y + dot, x + dot - 1 - cut, y + dot), fill=hi)
            d.rectangle((x, y + cut, x + dot - 1, y + dot - 1 - cut), fill=hole)
            d.rectangle((x + cut, y, x + dot - 1 - cut, y + dot - 1), fill=hole)


def border(system, panel, pal):
    W, H, s = PANELS[panel]
    x, y, w, h = rect_for(system, W, H)
    name, icon_file, accent, _, _ = SYSTEMS[system]
    bg = hexrgb(pal["screenBg"])
    acc = hexrgb(accent)
    img = Image.new("RGBA", (W, H), bg + (255,))

    # the menu's screen: dot grid (4x4 CSS px tile, dots at (0,0) and (2,2), device pixel lit when its centre is
    # inside a dot: RndsRaster), dark edge 4 CSS px and the lighter band 8 CSS px at the top
    def lit(p, off):
        u = ((p + 0.5) / s - off) % 4.0
        return u < 1
    ca = [lit(i, 0) for i in range(W)]
    cb = [lit(i, 2) for i in range(W)]
    dA, dB = pal["dotA"], pal["dotB"]
    colA = mix(bg, dA[:3], dA[3])
    colB = mix(bg, dB[:3], dB[3])
    px = img.load()
    for j in range(H):
        ra, rb = lit(j, 0), lit(j, 2)
        if not (ra or rb):
            continue
        for i in range(W):
            if ra and ca[i]:
                px[i, j] = colA + (255,)
            elif rb and cb[i]:
                px[i, j] = colB + (255,)
    band = round(8 * s)
    edge = round(4 * s)
    d = ImageDraw.Draw(img)
    d.rectangle((0, 0, W - 1, band - 1), fill=hexrgb(pal["screenBand"]))
    fr = hexrgb(pal["screenFrame"])
    d.rectangle((0, 0, W - 1, edge - 1), fill=fr)
    d.rectangle((0, H - edge, W - 1, H - 1), fill=fr)
    d.rectangle((0, 0, edge - 1, H - 1), fill=fr)
    d.rectangle((W - edge, 0, W - 1, H - 1), fill=fr)

    # the well the picture sits in: a soft shadow down-right, the system's colour as a thin rim (low contrast:
    # mixed most of the way into the background), the dark well, then the hole
    gap = round(3 * s)       # dark well between the rim and the picture
    rim = max(2, round(2 * s))
    rad = round(4 * s)
    sh = round(4 * s)
    shadow = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    ImageDraw.Draw(shadow).rectangle((x - gap - rim + sh, y - gap - rim + sh, x + w + gap + rim + sh - 1,
                                      y + h + gap + rim + sh - 1), fill=pal["hole"][:3] + (round(pal["hole"][3] * 255),))
    img.alpha_composite(shadow)
    rim_col = mix(bg, acc, 0.42)
    pixel_rrect(d, (x - gap - rim, y - gap - rim, x + w + gap + rim, y + h + gap + rim), rad, rim_col)
    pixel_rrect(d, (x - gap, y - gap, x + w + gap, y + h + gap), max(0, rad - rim), hexrgb(pal["well"]))

    left, top = x, y
    rim_out = gap + rim + sh       # what the well takes from each margin

    # speaker grilles in the side margins when they are wide enough (the DS Lite's dots beside its top screen)
    pitch = round(7 * s)
    dot = round(3 * s)
    side_room = left - rim_out - edge
    hole_col = mix(bg, hexrgb(pal["well"]), 0.75 if sum(bg) < 384 else 0.3)
    hi_col = mix(bg, (255, 255, 255), 0.1 if sum(bg) < 384 else 0.6)
    if side_room >= 26 * s:
        cols = 5 if side_room >= 50 * s else 3
        rows = 7
        cl = (edge + left - rim_out) // 2
        for cx in (cl, W - cl):
            grille(img, int(cx), H // 2, cols | 1, rows, pitch, dot, hole_col, hi_col)

    # the system's name, letter-spaced, with its icon: in the bottom margin when it is tall enough, else under the
    # left grille when the sides are wide
    ts = 2 if s > 1.2 else 1
    label = spaced(name, ts, 2 * ts)
    ink = mix(bg, hexrgb(pal["muted"]), pal["label"])
    ic = Image.open(os.path.join(ICONS, icon_file)).convert("RGBA")
    ik = 2 if s > 1.2 else 1
    ic = ic.resize((ic.width * ik, ic.height * ik), Image.NEAREST)
    # the icon dimmed toward the background, so it reads as part of the border
    flat = Image.new("RGBA", ic.size, bg + (255,))
    ic_dim = Image.blend(flat, Image.alpha_composite(flat, ic), 0.55)
    ic_dim.putalpha(ic.getchannel("A"))
    bottom_room = H - (y + h) - rim_out - edge
    if bottom_room >= label.height + 10 * s:
        gapx = round(6 * s)
        total = ic_dim.width + gapx + label.width if bottom_room >= ic_dim.height + 6 * s else label.width
        bx = (W - total) // 2
        by_mid = y + h + rim_out + (H - edge - (y + h + rim_out)) // 2
        if total != label.width:
            img.alpha_composite(ic_dim, (bx, by_mid - ic_dim.height // 2))
            bx += ic_dim.width + gapx
        img.paste(ink + (255,), (bx, by_mid - label.height // 2), label)
    elif side_room >= ic_dim.width + 8 * s:
        cx = (edge + left - rim_out) // 2
        img.alpha_composite(ic_dim, (cx - ic_dim.width // 2, H - edge - round(16 * s) - ic_dim.height))

    # the hole: fully clear, exactly the picture's rectangle
    ImageDraw.Draw(img).rectangle((x, y, x + w - 1, y + h - 1), fill=(0, 0, 0, 0))
    return img, (x, y, w, h)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "system", "rootfs", "usr", "share", "banana", "borders")
    pals = theme_palettes()
    lines = ["# Banana's screen borders (tools/make-borders.py, generated): <panel mode> <system> <x> <y> <w> <h>, the game",
             "# picture's rectangle in the border's hole (RetroArch's custom viewport). A system not listed for a panel",
             "# fills it, so it has no border there."]
    for panel, (W, H, s) in PANELS.items():
        for system in SYSTEMS:
            x, y, w, h = rect_for(system, W, H)
            if (w, h) == (W, H):
                continue
            lines.append(f"{panel} {system} {x} {y} {w} {h}")
            for theme, pal in pals.items():
                img, _ = border(system, panel, pal)
                dd = os.path.join(out, panel, theme)
                os.makedirs(dd, exist_ok=True)
                img.save(os.path.join(dd, system + ".png"), optimize=True)
                with open(os.path.join(dd, system + ".cfg"), "w") as f:
                    f.write(f"# Banana's screen border: {SYSTEMS[system][0].title()}, {W}x{H}, {theme} "
                            f"(tools/make-borders.py)\noverlays = 1\noverlay0_overlay = \"{system}.png\"\n"
                            "overlay0_full_screen = true\noverlay0_normalized = true\noverlay0_descs = 0\n")
    with open(os.path.join(out, "borders.conf"), "w") as f:
        f.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()

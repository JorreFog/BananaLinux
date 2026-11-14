#!/usr/bin/env python3
"""Activity: native-look mockups (BananaOS menu style).

Mirrors BananaOS's menu primitives from ui/rnds/RndsUI.cpp + ui/rnds/RndsRaster.cpp:
  * the 4x4 dither tile (white 14% at (0,0), 7% at (2,2))
  * CSS-like boxes: fill, hard-stop gradient bands, 3 px borders, inset and
    outer shadows (blur 0), radii 2..18
  * screen chrome: 4 px frame, band at the top, tinted "sheet" (color-mix of
    the system accent 34% over #15141a), bubbles, key chips, face chips
  * Pixelify Sans (Regular/Medium), whole-pixel sizes, letter spacing, uppercase
Renders the Activity app's two views, both screens, at 640x480 and 1024x768
(the RG DS Plus, 1.6x), dark and light.
"""
import os
from PIL import Image, ImageChops, ImageDraw, ImageFont

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "..", ".."))
FONT_DIR = os.path.join(ROOT, "ui", "assets", "rnds", "fonts")
OUT = os.path.dirname(os.path.abspath(__file__))

# ------------------------------------------------------------------ palette
DARK = dict(
    ink="#f1ede2", muted="#a39d8e", line="#8a8374", edge="#e8e2d3", navy="#fff6dc", red="#ff3b4a",
    screenBg="#17161c", screenFrame="#0b0a0d", screenBand="#2e2b33",
    sheetBase="#15141a", panelBand="#3a3540", panelBody="#17161c", panelLow="#0b0a0d",
    chipBand="#2e2b33", chipBody="#121117", bubbleBand="#38333b", bubbleBody="#191820", bubbleLow="#0f0e13",
    trackBg="#100f14", meterBg="#15141a", rule="#45403d", brandBg="#262219", railBg="#100f14",
    kbdBg="#f1ede2", kbdInk="#17161c", blackish="#0b0a0d", dither=(1, 1, 1), ditherA=0.14, ditherB=0.07,
    shadow=(0, 0, 0, 1.0),
)
LIGHT = dict(
    ink="#2a2620", muted="#6b6252", line="#a69d8c", edge="#3a342c", navy="#fffaf0", red="#e23b4a",
    screenBg="#f3efe6", screenFrame="#d4ccbd", screenBand="#fdfcf9",
    sheetBase="#fdfbf6", panelBand="#ffffff", panelBody="#f6f2ea", panelLow="#d8d0c0",
    chipBand="#ffffff", chipBody="#f6f2ea", bubbleBand="#ffffff", bubbleBody="#faf7f0", bubbleLow="#ddd5c6",
    trackBg="#e6dfd0", meterBg="#e3dccd", rule="#d6cfc0", brandBg="#262219", railBg="#e6dfd0",
    kbdBg="#3a342c", kbdInk="#f2f6fa", blackish="#3a2e14", dither=(58, 46, 20), ditherA=0.07, ditherB=0.035,
    shadow=(58, 46, 20, 0.25),
)
ACCENT = dict(base="#f0b429", light="#ffe68a", dark="#8a5a00", band="#ffcf5c", low="#c98f12")


def hx(s, a=255):
    s = s.lstrip("#")
    if len(s) == 3:
        s = "".join(c * 2 for c in s)
    return (int(s[0:2], 16), int(s[2:4], 16), int(s[4:6], 16), a)


def mix(c1, p, c2):
    return tuple(int(round(c1[i] * p + c2[i] * (1 - p))) for i in range(4))


def with_a(c, a):
    return (c[0], c[1], c[2], max(0, min(255, int(round(a * 255)))))


# ------------------------------------------------------------------ text
_fonts = {}


def font(size, medium=False):
    key = (size, medium)
    if key not in _fonts:
        _fonts[key] = ImageFont.truetype(
            os.path.join(FONT_DIR, "PixelifySans-Medium.ttf" if medium else "PixelifySans-Regular.ttf"), size)
    return _fonts[key]


def text_w(s, size_px, medium=False, ls_px=0.0, upper=False):
    if upper:
        s = s.upper()
    f = font(size_px, medium)
    return sum(f.getlength(ch) for ch in s) + ls_px * max(0, len(s) - 1)


K = 3  # supersampling for shapes


def rr(size, radius, fill):
    img = Image.new("RGBA", (size[0] * K, size[1] * K), (0, 0, 0, 0))
    ImageDraw.Draw(img).rounded_rectangle([0, 0, size[0] * K - 1, size[1] * K - 1],
                                          radius=max(0, radius) * K, fill=fill)
    return img


def paint_box(w, h, r=0, fill=None, bands=(), border=0, borderColor=None, insets=(), outer=(),
              dither=False):
    """CSS-like box at device pixels. Returns (RGBA image, x_offset, y_offset)."""
    pad = 2
    for dx, dy, spread, col in outer:
        pad = max(pad, int(abs(dx) + spread) + 2, int(abs(dy) + spread) + 2)
    W, H = w + 2 * pad, h + 2 * pad
    img = Image.new("RGBA", (W * K, H * K), (0, 0, 0, 0))
    # outer shadows (blur 0)
    for dx, dy, spread, col in outer:
        sh = Image.new("RGBA", (W * K, H * K), (0, 0, 0, 0))
        d = ImageDraw.Draw(sh)
        d.rounded_rectangle([(pad + dx - spread) * K, (pad + dy - spread) * K,
                             (pad + dx + w + spread) * K - 1, (pad + dy + h + spread) * K - 1],
                            radius=max(0, r + spread) * K, fill=col)
        img.alpha_composite(sh)
    # body: fill + hard-stop bands, clipped to the rounded rect
    mask = Image.new("L", (W * K, H * K), 0)
    ImageDraw.Draw(mask).rounded_rectangle([pad * K, pad * K, (pad + w) * K - 1, (pad + h) * K - 1],
                                           radius=r * K, fill=255)
    body = Image.new("RGBA", (W * K, H * K), (0, 0, 0, 0))
    if fill:
        body.paste(fill, (0, 0, W * K, H * K))
    for y0, y1, col in bands:
        top = (pad + max(0, y0)) * K
        bot = (pad + (h if y1 < 0 else min(h, y1))) * K
        body.paste(col, (pad * K, top, (pad + w) * K, bot))
    body.putalpha(Image.composite(mask, Image.new("L", mask.size, 0), mask))
    img.alpha_composite(body)
    # inset shadows: the band between the mask and its shifted copy
    for dx, dy, spread, col in insets:
        shifted = Image.new("L", mask.size, 0)
        shifted.paste(mask, (int(dx * K), int(dy * K)))
        edge = Image.eval(Image.composite(Image.new("L", mask.size, 0), mask, shifted), lambda v: 255 - v)
        alpha = ImageChops.multiply(ImageChops.multiply(edge, mask), Image.new("L", mask.size, col[3]))
        band = Image.new("RGBA", mask.size, col)
        band.putalpha(alpha)
        img.alpha_composite(band)
    # border (inside the box)
    if border and borderColor:
        bl = Image.new("RGBA", (W * K, H * K), (0, 0, 0, 0))
        ImageDraw.Draw(bl).rounded_rectangle(
            [pad * K, pad * K, (pad + w) * K - 1, (pad + h) * K - 1],
            radius=r * K, outline=borderColor, width=max(1, border * K))
        img.alpha_composite(bl)
    out = img.resize((W, H), Image.LANCZOS)
    # dither inside the border only
    if dither and fill:
        step = max(1, int(round(S)))
        imask = Image.new("L", (W, H), 0)
        ImageDraw.Draw(imask).rounded_rectangle(
            [pad + border, pad + border, pad + w - border - 1, pad + h - border - 1],
            radius=max(0, r - border), fill=255)
        tile = Image.new("RGBA", (W, H), (0, 0, 0, 0))
        px = tile.load()
        dot = (*PAL["dither"], 255)
        for y in range(H):
            for x in range(W):
                u, v = ((x - pad) // step) % 4, ((y - pad) // step) % 4
                if u == 0 and v == 0:
                    px[x, y] = with_a(dot, PAL["ditherA"])
                elif u == 2 and v == 2:
                    px[x, y] = with_a(dot, PAL["ditherB"])
        tile.putalpha(Image.composite(tile.getchannel("A"), Image.new("L", tile.size, 0), imask))
        out = Image.alpha_composite(out, tile)
    return out, pad, pad


PAL = DARK
S = 1.0


class Screen:
    def __init__(self, scale=1.0, light=False):
        global S, PAL
        S, PAL = scale, (LIGHT if light else DARK)
        self.S, self.P = scale, PAL
        self.light = light
        self.W, self.H = int(round(640 * scale)), int(round(480 * scale))
        self.img = Image.new("RGBA", (self.W, self.H), hx(PAL["screenBg"]))
        self._dither_all()
        # frame: 4 px dark edge, an 8 px band at the top and a hairline inside
        d = ImageDraw.Draw(self.img)
        d.rounded_rectangle([0, 0, self.W - 1, self.H - 1], radius=self.u(2), outline=hx(PAL["screenFrame"]),
                            width=self.u(4))
        d.rectangle([self.u(4), self.u(4), self.W - self.u(5), self.u(11)], fill=with_a(hx(PAL["screenBand"]), 0.5))
        d.rectangle([self.u(4), self.u(11), self.W - self.u(5), self.u(12)], fill=with_a(hx("#ffffff"), 0.05))

    def u(self, v):
        return int(round(v * self.S))

    def _dither_all(self):
        step = max(1, int(round(self.S)))
        dark = PAL["dither"]
        px = self.img.load()
        for y in range(self.H):
            for x in range(self.W):
                u, v = (x // step) % 4, (y // step) % 4
                if u == 0 and v == 0:
                    px[x, y] = with_a((*dark, 255), PAL["ditherA"])
                elif u == 2 and v == 2:
                    px[x, y] = with_a((*dark, 255), PAL["ditherB"])

    def box(self, x, y, w, h, **kw):
        r = kw.pop("r", 0)
        img, ox, oy = paint_box(self.u(w), self.u(h), self.u(r), **kw)
        self.img.alpha_composite(img, (self.u(x) - ox, self.u(y) - oy))

    def text(self, s, x, y, size, color, medium=False, ls=0.0, upper=False, align="left", maxw=None,
             lineH=None):
        if upper:
            s = s.upper()
        px = max(4, int(round(size * self.S)))
        f = font(px, medium)
        asc, desc = f.getmetrics()
        lh = (lineH or size) * self.S
        if lineH is None:
            lh = asc + desc
        step = ls * self.S
        widths = [f.getlength(ch) for ch in s]
        tw = sum(widths) + step * max(0, len(s) - 1)
        if maxw is not None:
            lim = maxw * self.S
            while s and tw > lim:
                s, widths = s[:-1], widths[:-1]
                tw = sum(widths) + step * max(0, len(s) - 1)
        x0 = x * self.S
        if align == "center":
            x0 -= tw / 2
        elif align == "right":
            x0 -= tw
        y0 = y * self.S + (lh - (asc + desc)) / 2
        d = ImageDraw.Draw(self.img)
        if ls <= 0.01:
            # one run: FreeType's own kerning, as the menu's engine lays a line out
            d.text((x0, y0), s, font=f, fill=color)
        else:
            cx = x0
            for ch, wd in zip(s, widths):
                d.text((cx, y0), ch, font=f, fill=color)
                cx += wd + step
        return tw / self.S

    def hline(self, x, y, w, color, h=2):
        self.img.alpha_composite(Image.new("RGBA", (max(1, self.u(w)), max(1, self.u(h))), color),
                                 (self.u(x), self.u(y)))

    def rect(self, x, y, w, h, color):
        self.img.alpha_composite(Image.new("RGBA", (max(1, self.u(w)), max(1, self.u(h))), color),
                                 (self.u(x), self.u(y)))

    def tri(self, pts, color):
        canvas = Image.new("RGBA", (self.W * K, self.H * K), (0, 0, 0, 0))
        ImageDraw.Draw(canvas).polygon([(px * self.S * K, py * self.S * K) for px, py in pts], fill=color)
        self.img.alpha_composite(canvas.resize((self.W, self.H), Image.LANCZOS), (0, 0))


# ------------------------------------------------------------------ components
def key_chip(sc, x, y, label, size=16, h=23):
    tw = text_w(label, size * sc.S, True) / sc.S
    w = 12 + tw
    sc.box(x, y, w, h, r=4, fill=hx(sc.P["kbdBg"]))
    sc.text(label, x + 6, y + (h - size) / 2 - 1, size, hx(sc.P["kbdInk"]), medium=True)
    return w


def face_chip(sc, x, y, key, label, w=None, kind=0, pressed=False):
    P = sc.P
    if w is None:
        kw = 12 + text_w(key, 16 * sc.S, True) / sc.S if key else 0
        w = 15 + kw + (8 if key else 0) + text_w(label, 19 * sc.S, True) / sc.S + 15
    h, dy = 40, (2 if pressed else 0)
    fill = hx(P["panelBody"]) if kind != 1 else hx("#2a1016")
    bcol = hx(P["edge"]) if kind != 1 else hx(P["red"])
    top = hx(P["bubbleBand"]) if kind != 1 else hx("#5a2430")
    sc.box(x + dy, y + dy, w, h, r=10, fill=fill, bands=[(0, 8, top), (8, -1, fill)],
           border=3, borderColor=bcol,
           insets=[] if pressed else [(0, -4, 0, hx(P["panelLow"]))],
           outer=[(1, 1, 0, with_a(hx(P["blackish"]), 0.9))] if pressed else
                 [(3, 3, 0, with_a(hx(P["blackish"]), 0.9))])
    lx = x + 15 + dy
    color = hx(P["ink"]) if kind == 0 else hx("#ff8a96") if kind == 1 else hx(P["muted"])
    if key:
        kw = key_chip(sc, lx, y + 8.5 + dy, key)
        lx += kw + 8
    sc.text(label, lx, y + 9 + dy, 19, color, medium=True)
    return w


def stat(sc, x, y, value, label, vsize=26, lsize=12):
    sc.text(value, x, y, vsize, hx(sc.P["ink"]), medium=True)
    sc.text(label, x, y + vsize + 4, lsize, hx(sc.P["muted"]), ls=1.4, upper=True)


BANANA = [
    "..........SSS...",
    ".........SSSS...",
    "........SSYYG...",
    "......SSYYYYYG..",
    "....SSYYYYHHYYG.",
    "...SYYYYHYYYYYYG",
    "..SYYYYGGYYYYYYG",
    "..SYYYGGYYYYYYGG",
    "..SYYGYYYYYYYYG.",
    "...SGYYYYYYYYGG.",
    "....YYYYYYYYGG..",
    "...SYYYYYYYGG...",
    "...SSYYYYGGG....",
    "....SSGGGG......",
]


def banana(sc, x, y):
    cols = {"S": "#7a5a12", "Y": "#f6c437", "G": "#3f8a3a", "H": "#ffe9a0"}
    for row, line in enumerate(BANANA):
        for col, ch in enumerate(line):
            if ch in cols:
                sc.rect(x + col, y + row, 1, 1, hx(cols[ch]))


def wifi_icon(sc, x, y, color):
    k = 4
    tile = Image.new("RGBA", (26 * k, 16 * k), (0, 0, 0, 0))
    d = ImageDraw.Draw(tile)
    for r in (4, 8, 12):
        d.arc([(13 - r) * k, (14 - r) * k, (13 + r) * k, (14 + r) * k], 210, 330, fill=color,
              width=int(2.2 * k))
    d.ellipse([(11.2) * k, (11.2) * k, (14.8) * k, (14.8) * k], fill=color)
    tile = tile.resize((26, 16), Image.LANCZOS)
    # the icon's box at 1x is 26 x 16 CSS px; scale before pasting
    if sc.S != 1.0:
        tile = tile.resize((sc.u(26), sc.u(16)), Image.LANCZOS)
    sc.img.alpha_composite(tile, (sc.u(x), sc.u(y)))


def status_row(sc, clock="10:57", date="11/06", pct=82):
    P = sc.P
    wifi_icon(sc, 16, 23, with_a(hx(P["ink"]), 0.95))
    sc.text("%d%%" % pct, 44, 20, 19, hx(P["ink"]))
    # battery
    sc.box(78, 25, 26, 14, r=3, border=2, borderColor=hx(P["ink"]))
    sc.rect(104, 29, 3, 6, hx(P["ink"]))
    sc.rect(81, 28, 20 * pct // 100, 8, hx(P["ink"]))
    # brand plate
    bx = 124
    sc.box(bx, 19, 132, 26, r=4, fill=hx(P["brandBg"]),
           outer=[(2, 2, 0, with_a(hx(P["blackish"]), 0.9))])
    banana(sc, bx + 8, 26)
    sc.text("ACTIVITY", bx + 30, 24, 16, hx("#fffaf0"), medium=True, ls=1.4)
    cw = text_w(clock, 20 * sc.S) / sc.S
    dw = text_w(date, 19 * sc.S) / sc.S
    sc.text(date, 622 - cw - 8 - dw, 20, 19, hx(P["ink"]))
    sc.text(clock, 622 - cw, 20, 20, hx(P["ink"]), medium=True)


def sheet(sc):
    P = sc.P
    fill = mix(hx(ACCENT["base"]), 0.34, hx(P["sheetBase"]))
    sc.box(18, 60, 604, 406, r=18, fill=fill, dither=True, border=3, borderColor=hx(P["edge"]),
           insets=[(0, 6, 0, with_a(hx("#ffffff"), 0.10)), (0, -8, 0, with_a(hx(P["blackish"]), 0.35))],
           outer=[(4, 4, 0, with_a(hx(P["blackish"]), 0.45))])


def period_chips(sc, active="ALL TIME", right=598, y=84):
    P = sc.P
    x = right
    for s in ["ALL TIME", "MONTH", "WEEK"]:
        w = text_w(s, 12 * sc.S, True, 0.8) / sc.S + 18
        x -= w
        on = s == active
        sc.box(x, y, w, 22, r=8, fill=hx(ACCENT["base"]) if on else hx(P["chipBody"]),
               bands=[] if on else [(0, 6, hx(P["chipBand"]))],
               border=2, borderColor=hx(ACCENT["light"]) if on else with_a(hx(P["line"]), 0.9),
               outer=[(2, 2, 0, with_a(hx(P["blackish"]), 0.8))])
        sc.text(s, x + w / 2, y + 4, 12, hx("#17161c") if on else hx(P["muted"]), medium=True,
                ls=0.8, align="center")
        x -= 6


def bubble(sc, x, y, w, h, title, sub):
    P = sc.P
    sc.box(x, y, w, h, r=12, fill=hx(P["bubbleBody"]), border=3, borderColor=hx(P["line"]),
           bands=[(0, 10, hx(P["bubbleBand"])), (10, -1, hx(P["bubbleBody"]))],
           insets=[(0, -6, 0, hx(P["bubbleLow"]))],
           outer=[(4, 4, 0, with_a(hx(P["blackish"]), 0.9))])
    t = text_w(title, 26 * sc.S, True, 0.52) / sc.S
    sc.text(title, x + w / 2 - t / 2, y + 11, 26, hx(P["ink"]), medium=True, ls=0.52)
    s = text_w(sub, 14 * sc.S, False, 1.3) / sc.S
    sc.text(sub, x + w / 2 - s / 2, y + 47, 14, hx(P["muted"]), ls=1.3, upper=True)
    cx, by = x + w / 2, y + h - 3
    sc.tri([(cx - 16, by), (cx + 16, by), (cx, by + 16)], hx(P["line"]))
    sc.tri([(cx - 12, by + 3), (cx + 12, by + 3), (cx, by + 14)], hx(P["bubbleBody"]))


def arrows(sc, x, y, right=False):
    P = sc.P
    sc.box(x, y, 22, 22, r=5, fill=hx(P["chipBody"]), bands=[(0, 5, hx(P["chipBand"]))],
           border=2, borderColor=with_a(hx(P["line"]), 0.9))
    if right:
        sc.tri([(x + 8, y + 6), (x + 8, y + 16), (x + 15, y + 11)], hx(P["ink"]))
    else:
        sc.tri([(x + 14, y + 6), (x + 14, y + 16), (x + 7, y + 11)], hx(P["ink"]))


# ------------------------------------------------------------------ data
GAMES = [
    ("Pokémon Platinum", "nds", "32h 18m", "48 plays", "2h ago", 1.00, False),
    ("Golden Sun", "gba", "21h 05m", "31 plays", "3d ago", 0.65, False),
    ("Tetris Worlds", "gba", "14h 47m", "62 plays", "12d ago", 0.46, True),
    ("Castlevania: Aria of Sorrow", "gba", "12h 30m", "22 plays", "5d ago", 0.39, False),
    ("Döda Kvarter", "app", "8h 12m", "19 plays", "yesterday", 0.25, False),
]
SYSTEM_COLOR = {"nds": "#3d9e9a", "gba": "#8b6ad8", "snes": "#a07ad4", "gbc": "#2eae62",
                "psx": "#5c86e0", "app": "#e8a33d"}
SYS_NAME = {"nds": "NDS", "gba": "GBA", "snes": "SNES", "gbc": "GBC", "psx": "PSX", "app": "APP"}
SESSIONS = [
    ("09:12", "Pokémon Platinum", "nds", "48m"),
    ("12:30", "Tetris Worlds", "gba", "22m"),
    ("14:05", "Pokémon Platinum", "nds", "1h 04m"),
    ("20:10", "Döda Kvarter", "app", "38m"),
    ("22:03", "Golden Sun", "gba", "20m"),
]


# ------------------------------------------------------------------ top screens
def top_library(sc):
    P = sc.P
    status_row(sc)
    sheet(sc)
    ink, muted, line = hx(P["ink"]), hx(P["muted"]), hx(P["line"])
    sc.text("ALL TIME PLAYED", 42, 92, 13, muted, ls=1.4, upper=True)
    period_chips(sc, "ALL TIME")
    sc.text("128h 42m", 40, 114, 44, hx(P["ink"]) if sc.light else hx(P["navy"]), medium=True, ls=0.5)
    sc.tri([(44, 172), (52, 172), (48, 166)], hx(ACCENT["base"]))
    dw = sc.text("+6h 12m", 58, 164, 14, hx(ACCENT["base"]), medium=True, ls=0.6, upper=True)
    sc.text("vs previous 30 days", 58 + dw + 10, 165, 12, muted, ls=1.2, upper=True)
    for i, (v, l) in enumerate([("214", "SESSIONS"), ("36m", "AVG SESSION"),
                                ("62 / 91", "ACTIVE DAYS"), ("NDS", "TOP SYSTEM")]):
        stat(sc, 330 + (i % 2) * 138, 118 + (i // 2) * 58, v, l)
    sc.hline(316, 120, 2, with_a(line, 0.45), h=92)
    sc.hline(42, 236, 556, with_a(line, 0.30), h=2)
    sc.text("THIS WEEK", 42, 248, 13, muted, ls=1.4, upper=True)
    sc.text("PEAK 4H 12M · 24 SESSIONS", 598, 248, 12, muted, ls=1.2, upper=True, align="right")
    days = ["MON", "TUE", "WED", "THU", "FRI", "SAT", "SUN"]
    vals = [1.6, 2.4, 0.9, 4.2, 2.0, 3.1, 1.2]
    top, base = 282, 378
    bw, gap = 40, 30
    x0 = 42 + (556 - (7 * bw + 6 * gap)) / 2
    mx = max(vals)
    for i, (d, v) in enumerate(zip(days, vals)):
        x = x0 + i * (bw + gap)
        sc.box(x, top, bw, base - top, r=8, fill=hx(P["trackBg"]), dither=True, border=2,
               borderColor=with_a(line, 0.55))
        fh = 6 + (base - top - 16) * v / mx
        today = d == "THU"
        col = hx("#ffd76a") if today else hx(ACCENT["base"])
        sc.box(x + 3, base - fh, bw - 6, fh, r=6, fill=col, bands=[(0, 4, hx(ACCENT["light"]))])
        sc.text(d, x + bw / 2, base + 10, 12, ink if today else muted, medium=today, ls=1.2, align="center")
        if today:
            sc.rect(x + bw / 2 - 5, base + 28, 10, 3, hx(ACCENT["base"]))
    sc.text("LONGEST 2H 04M · 3 DAY STREAK · 2 GAMES A DAY", 42, 416, 12, muted, ls=1.2, upper=True)


def top_timeline(sc):
    P = sc.P
    status_row(sc)
    sheet(sc)
    ink, muted, line = hx(P["ink"]), hx(P["muted"]), hx(P["line"])
    sc.text("DAILY TIMELINE", 42, 92, 13, muted, ls=1.4, upper=True)
    period_chips(sc, "WEEK")
    sc.text("THURSDAY", 42, 116, 13, hx(ACCENT["dark"] if sc.light else ACCENT["base"]), ls=1.6, upper=True)
    sc.text("06 NOV", 40, 132, 40, hx(P["ink"]) if sc.light else hx(P["navy"]), medium=True, ls=0.5)
    sc.text("2026", 40 + text_w("06 NOV", 40 * sc.S, True, 0.5) / sc.S + 16, 152, 15, muted, medium=True, ls=1.4,
            upper=True)
    arrows(sc, 42, 182)
    arrows(sc, 70, 182, right=True)
    sc.text("WED 05 · FRI 07", 104, 186, 12, muted, ls=1.2, upper=True)
    for i, (v, l) in enumerate([("7", "SESSIONS"), ("3h 12m", "TIME"), ("27m", "AVG"), ("22:41", "LAST")]):
        stat(sc, 330 + (i % 2) * 138, 118 + (i // 2) * 58, v, l)
    sc.hline(316, 120, 2, with_a(line, 0.45), h=92)
    sc.hline(42, 236, 556, with_a(line, 0.30), h=2)
    sc.text("ACTIVITY BY HOUR", 42, 248, 13, muted, ls=1.4, upper=True)
    sc.text("PEAK 14:00", 598, 248, 12, muted, ls=1.2, upper=True, align="right")
    hours = [0.0] * 24
    for h, v in {9: .55, 10: .8, 12: .35, 14: 1.0, 15: .7, 20: .6, 22: .4}.items():
        hours[h] = v
    top, base = 276, 362
    bw, gap = 12, 8
    x0 = 42 + (556 - (24 * bw + 23 * gap)) / 2
    # hour grid: a dot column every 6 h, and the baseline
    for h in (0, 6, 12, 18, 23):
        gx = x0 + h * (bw + gap) + (bw / 2 if h < 23 else bw)
        for gy in range(top, base, 10):
            sc.rect(gx - 1, gy, 2, 3, with_a(line, 0.20))
    sc.hline(x0 - 6, base, 24 * bw + 23 * gap + 12, with_a(line, 0.45), h=2)
    for i, v in enumerate(hours):
        x = x0 + i * (bw + gap)
        if v <= 0:
            continue
        fh = 6 + (base - top) * v
        col = mix(hx(ACCENT["low"]), 0.35 + 0.65 * v, hx(ACCENT["base"]))
        sc.box(x, base - fh, bw, fh, r=3, fill=col, bands=[(0, 3, with_a(hx(ACCENT["light"]), 0.85))])
    for h, lab in ((0, "00"), (6, "06"), (12, "12"), (18, "18"), (23, "24")):
        x = x0 + h * (bw + gap) + (bw / 2 if h < 23 else bw)
        sc.text(lab, x, base + 6, 11, muted, ls=1.0, align="center")
    # time of day: one 24 h bar, four parts of it, and its legend
    sc.text("TIME OF DAY", 42, 388, 12, muted, ls=1.4, upper=True)
    parts = [("NIGHT", "0–6", 8, "#6b5320"), ("MORN", "6–12", 34, "#b8791a"),
             ("AFT", "12–18", 41, ACCENT["base"]), ("EVE", "18–24", 17, "#ffd76a")]
    bx, bwid, by, bh = 42, 556, 406, 18
    sc.box(bx, by, bwid, bh, r=6, fill=hx(P["trackBg"]), border=2, borderColor=with_a(line, 0.6))
    cx = bx + 2
    for n, rng, pct, col in parts:
        seg = (bwid - 4) * pct / 100
        sc.box(cx, by + 2, seg, bh - 4, r=4, fill=hx(col))
        cx += seg
    for i, (n, rng, pct, col) in enumerate(parts):
        sc.box(bx + i * 139, 428, 8, 8, r=2, fill=hx(col))
        sc.text("%s %s · %d%%" % (n, rng, pct), bx + i * 139 + 13, 427, 11, muted, ls=1.0, upper=True)


# ------------------------------------------------------------------ bottom screens
def shoulders(sc, left_label, right_label):
    P = sc.P
    kl = key_chip(sc, 18, 21.5, "L")
    sc.text(left_label, 18 + kl + 8, 21, 19, hx(P["ink"]), ls=1.2, upper=True)
    sc.box(284, 31.5, 72, 3, r=2, fill=hx(P["rule"]))
    kr = 12 + text_w("R", 16 * sc.S, True) / sc.S
    sc.box(622 - kr, 21.5, kr, 23, r=4, fill=hx(P["kbdBg"]))
    sc.text("R", 622 - kr + 6, 24.5, 16, hx(P["kbdInk"]), medium=True)
    w = text_w(right_label, 19 * sc.S, False, 1.2) / sc.S
    sc.text(right_label, 622 - kr - 8 - w, 21, 19, hx(P["ink"]), ls=1.2, upper=True)


def list_row(sc, x, y, w, h, sel, strip=None):
    P = sc.P
    fill = mix(hx(ACCENT["base"]), 0.16, hx(P["panelBody"])) if sel else hx(P["panelBody"])
    sc.box(x, y, w, h, r=12, fill=fill, bands=[(0, 6, hx(P["panelBand"])), (6, -1, fill)],
           border=3, borderColor=hx(P["edge"]) if sel else hx(P["line"]),
           insets=[] if sel else [(0, -4, 0, hx(P["panelLow"]))],
           outer=[(3, 3, 0, with_a(hx(P["blackish"]), 0.85))])
    if strip:
        sc.box(x, y + 3, 4, h - 6, r=2, fill=hx(strip))


def face_width(sc, key, label):
    kw = 12 + text_w(key, 16 * sc.S, True) / sc.S if key else 0
    return 15 + kw + (8 if key else 0) + text_w(label, 19 * sc.S, True) / sc.S + 15


def footer(sc, left, mids, right):
    """left/right: (key, label); mids: list of (key, label); laid out like the menu's face row."""
    lw = face_width(sc, *left)
    rw = face_width(sc, *right)
    face_chip(sc, 18, 422, left[0], left[1], w=lw)
    face_chip(sc, 622 - rw, 422, right[0], right[1], w=rw)
    widths = [face_width(sc, k, l) for k, l in mids]
    total = sum(widths) + 10 * (len(widths) - 1)
    x = 18 + lw + (604 - lw - rw - total) / 2
    for (k, l), w in zip(mids, widths):
        face_chip(sc, x, 422, k, l, w=w)
        x += w + 10


def bottom_library(sc):
    P = sc.P
    ink, muted = hx(P["ink"]), hx(P["muted"])
    shoulders(sc, "WEEK", "MONTH")
    bubble(sc, 66, 58, 508, 84, "Pokémon Platinum", "NINTENDO DS · 48 SESSIONS · LAST 2H AGO")
    top, rowh, gap = 158, 40, 6
    for i, (title, sysid, time, plays, last, share, dusty) in enumerate(GAMES):
        y = top + i * (rowh + gap)
        sel = i == 0
        list_row(sc, 24, y, 588, rowh, sel, strip=ACCENT["base"] if sel else SYSTEM_COLOR[sysid])
        sc.text("%02d" % (i + 1), 38, y + 11, 13, hx(ACCENT["base"]) if sel else muted, medium=True, ls=0.8)
        sc.text(title, 64, y + 3, 19, ink, medium=True, maxw=260)
        sub = "%s · %s · %s" % (SYS_NAME[sysid], plays, last)
        if dusty:
            sub = "DUSTY · " + sub
        sc.text(sub, 64, y + 23, 10, hx(ACCENT["base"]) if dusty and not sel else muted, ls=1.1, upper=True)
        sc.text(time, 596, y + 8, 18, hx(P["navy"]) if sel else ink, medium=True, align="right")
        sc.box(470, y + 27, 126, 5, r=3, fill=hx(P["meterBg"]), border=0)
        sc.box(470, y + 27, max(4, 126 * share), 5, r=3,
               fill=hx("#ffd76a") if sel else hx(ACCENT["base"]))
    sc.box(620, top, 5, 226, r=2, fill=hx(P["railBg"]), dither=True, border=0)
    sc.box(619, top + 8, 7, 34, r=2, fill=hx(ACCENT["base"]), bands=[(0, 4, hx(ACCENT["light"]))],
           border=2, borderColor=hx(P["edge"]))
    sc.text("01", 584, 392, 15, ink, medium=True, align="right")
    sc.text("/ 42", 588, 394, 13, muted)
    footer(sc, ("X", "System"), [("SELECT", "Timeline"), ("START", "Exit")], ("Y", "Sort"))


def bottom_timeline(sc):
    P = sc.P
    ink, muted, line = hx(P["ink"]), hx(P["muted"]), hx(P["line"])
    shoulders(sc, "WEEK", "MONTH")
    bubble(sc, 66, 58, 508, 84, "THURSDAY 06 NOV", "7 SESSIONS · 3H 12M · +42M VS WED")
    top, rowh, gap = 158, 40, 6
    for i, (t, title, sysid, dur) in enumerate(SESSIONS):
        y = top + i * (rowh + gap)
        sel = i == 2
        list_row(sc, 24, y, 588, rowh, sel, strip=ACCENT["base"] if sel else SYSTEM_COLOR[sysid])
        sc.text(t, 64, y + 10, 18, ink, medium=True, ls=0.6)
        sc.text(title, 142, y + 10, 18, ink, medium=True, maxw=248)
        tw = text_w(SYS_NAME[sysid], 10 * sc.S, True, 1.0) / sc.S + 12
        sc.box(412, y + 11, tw, 17, r=4, fill=with_a(hx(SYSTEM_COLOR[sysid]), 0.20), border=1,
               borderColor=with_a(hx(SYSTEM_COLOR[sysid]), 0.8))
        sc.text(SYS_NAME[sysid], 418, y + 14, 10, hx(P["ink"]), medium=True, ls=1.0)
        sc.text(dur, 596, y + 9, 18, hx(P["ink"]), medium=True, align="right")
    sc.box(620, top, 5, 226, r=2, fill=hx(P["railBg"]), dither=True, border=0)
    sc.box(619, top + 100, 7, 36, r=2, fill=hx(ACCENT["base"]), bands=[(0, 4, hx(ACCENT["light"]))],
           border=2, borderColor=hx(P["edge"]))
    sc.text("03", 584, 392, 15, ink, medium=True, align="right")
    sc.text("/ 07", 588, 394, 13, muted)
    footer(sc, ("X", "System"), [("SELECT", "Library"), ("START", "Exit")], ("Y", "Sort"))


# ------------------------------------------------------------------ output
def pair(prefix, top_fn, bot_fn, scale=1.0, light=False):
    a = Screen(scale, light)
    top_fn(a)
    b = Screen(scale, light)
    bot_fn(b)
    a.img.convert("RGB").save(os.path.join(OUT, prefix + "-top.png"))
    b.img.convert("RGB").save(os.path.join(OUT, prefix + "-bottom.png"))
    return a, b


def compose(prefix, a, b, bg="#0b0a0d", gap=26, pad=30, label=None):
    w, h = a.img.size
    canvas = Image.new("RGBA", (w + 2 * pad, 2 * h + gap + 2 * pad), hx(bg))
    canvas.alpha_composite(a.img, (pad, pad))
    canvas.alpha_composite(b.img, (pad, pad + h + gap))
    canvas.convert("RGB").save(os.path.join(OUT, prefix + ".png"))
    return canvas


if __name__ == "__main__":
    # dark, 640x480
    a, b = pair("01-library-dark", top_library, bottom_library)
    compose("library-dark", a, b)
    c, d = pair("02-timeline-dark", top_timeline, bottom_timeline)
    compose("timeline-dark", c, d)
    # light
    e, f = pair("03-library-light", top_library, bottom_library, light=True)
    compose("library-light", e, f, bg="#2a2620")
    g, h = pair("03-timeline-light", top_timeline, bottom_timeline, light=True)
    compose("timeline-light", g, h, bg="#2a2620")
    # the Plus, 1.6x
    i, j = pair("04-library-plus", top_library, bottom_library, scale=1.6)
    compose("library-plus", i, j)
    k, l = pair("04-timeline-plus", top_timeline, bottom_timeline, scale=1.6)
    compose("timeline-plus", k, l)

    # a board with the two views (both screens) at 640
    pad, gap = 30, 26
    w, h = a.img.size
    board = Image.new("RGBA", (w * 2 + pad * 3, h * 2 + gap + pad * 3 + 34), hx("#0b0a0d"))
    board.alpha_composite(a.img, (pad, pad + 34))
    board.alpha_composite(b.img, (pad, pad + 34 + h + gap))
    board.alpha_composite(c.img, (pad * 2 + w, pad + 34))
    board.alpha_composite(d.img, (pad * 2 + w, pad + 34 + h + gap))
    d0 = ImageDraw.Draw(board)
    for label, x in (("LIBRARY  ·  OVERVIEW", pad), ("TIMELINE  ·  DAILY LOG", pad * 2 + w)):
        d0.text((x + 2, 14), label, font=ImageFont.truetype(os.path.join(FONT_DIR, "PixelifySans-Medium.ttf"), 18),
                fill=hx("#a39d8e"))
    board.convert("RGB").save(os.path.join(OUT, "00-overview.png"))
    print("written to", OUT)

#!/usr/bin/env python3
"""The README's pictures in banana-ui's own look: the warm charcoal panels with their dot grid, cream outlines, the
banana-yellow plate, Pixelify Sans drawn hard-edged and the pixel bunch. The chrome is drawn one art pixel at a time
and scaled up by a whole number; text is drawn hard-edged at its final size (a whole number of image pixels per font
pixel, so the font keeps its own spacing); the screenshots are the panels' own scanout grabs (an RG DS Plus, 1024x768
per panel).

    tools/make-readme-art.py <grabs dir> <out dir> [<thumbnails dir> [<flasher screenshot>]]

<thumbnails dir> holds <video id>.jpg, each video's YouTube thumbnail (i.ytimg.com/vi/<id>/maxresdefault.jpg), for the
video cards (VIDEOS); without it they are left out. [<flasher screenshot>] is docs/screenshot.png from
jesussaves-ai/banana-linux-flasher, for flasher.png. <grabs dir> holds <name>-top.png and <name>-bot.png for each screenshot pair in SHOTS, grabbed from both CRTCs on the
handheld. Writes into <out dir>: banner.png, superdrastic-logo.png, bananads-logo.png, emulators.png, stats.png,
rocknixds-moved.png (for the ROCKNIXDS README), flasher.png, h-<section>.png
shot-<name>.png and video-<id>.png."""
import os
import sys
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ASSETS = os.path.join(HERE, "..", "ui", "assets")
FONT = os.path.join(ASSETS, "rnds", "fonts", "PixelifySans-Medium.ttf")
ICONS = os.path.join(ASSETS, "rnds", "icons")
BANANA = os.path.join(ASSETS, "banana")

# banana-ui's dark palette (tools/banana-palette.sh), as the panels show it
BG = (11, 10, 13, 255)
PANEL = (23, 22, 28, 255)
CARD = (25, 24, 32, 255)
DOT = (38, 36, 44, 255)
LINE = (58, 53, 64, 255)
CREAM = (241, 237, 226, 255)
CREAM_DARK = (214, 208, 196, 255)
GREY = (163, 157, 142, 255)
YELLOW = (240, 180, 41, 255)
YELLOW_DARK = (201, 143, 18, 255)
TEAL = (35, 67, 70, 255)
TEAL_LIGHT = (51, 80, 83, 255)
BLACK = (0, 0, 0, 255)
WHITE = (255, 255, 255, 255)
BANANA_LIGHT = (255, 222, 82, 255)      # tools/make-logo.py's two-tone wordmark
BANANA_DARK = (238, 160, 24, 255)
RED_LIGHT = (255, 92, 76, 255)          # SuperDrastic's red, after DraStic's own
RED_DARK = (200, 34, 44, 255)
GREEN = (88, 200, 120, 255)
CLEAR = (0, 0, 0, 0)

GRID = 1000 / 92    # Pixelify Sans: at this size one font pixel is one image pixel (as in tools/make-logo.py)


def up(im, k):
    return im.resize((im.width * k, im.height * k), Image.NEAREST)


def mask(text, s=1):
    """The text's coverage, hard-edged, `s` image pixels per font pixel, cropped to the ink."""
    f = ImageFont.truetype(FONT, GRID * s)
    l, t, r, b = f.getbbox(text)
    im = Image.new("L", (int(r - l) + 8 * s, int(b) + 8 * s), 0)
    d = ImageDraw.Draw(im)
    d.fontmode = "1"
    d.text((4 * s - l, 4 * s), text, font=f, fill=255)
    return im.crop(im.getbbox())


def text(t, color, s=2):
    """One line of text in one colour, `s` image pixels per font pixel."""
    m = mask(t, s)
    out = Image.new("RGBA", m.size, CLEAR)
    out.paste(color, (0, 0), m)
    return out


def bold(m):
    o = Image.new("L", (m.width + 1, m.height), 0)
    o.paste(255, (0, 0), m)
    o.paste(255, (1, 0), m)
    return o


def shear(m, every):
    """Pixel italic: each row one pixel further right every `every` rows, counted from the bottom."""
    out = Image.new(m.mode, (m.width + m.height // every + 1, m.height), 0)
    for y in range(m.height):
        out.paste(m.crop((0, y, m.width, y + 1)), ((m.height - 1 - y) // every, y))
    return out


def dilate(m, r=1):
    o = Image.new("L", (m.width + 2 * r, m.height + 2 * r), 0)
    for dx in range(-r, r + 1):
        for dy in range(-r, r + 1):
            o.paste(255, (r + dx, r + dy), m)
    return o


def paint(m, light, dark, split=11 / 20):
    """Fill a mask in two tones, light over the top part, as the bananas' own shading and BANANAOS's wordmark."""
    fill = Image.new("RGBA", m.size, dark)
    ImageDraw.Draw(fill).rectangle([0, 0, m.width, int(m.height * split)], fill=light)
    out = Image.new("RGBA", m.size, CLEAR)
    out.paste(fill, (0, 0), m)
    return out


def word(parts, s=1, shadow=1, outline=0, italic=0, heavy=False):
    """Words side by side in two tones each [(text, light, dark)], with a drop shadow `shadow` pixels down and right,
    an `outline` of black around them, and a pixel italic."""
    whole = mask("".join(p[0] for p in parts), s)
    if heavy:
        whole = bold(whole)
    fill = Image.new("RGBA", whole.size, CLEAR)
    x = 0
    for i, (t, light, dark) in enumerate(parts):
        end = whole.width if i == len(parts) - 1 else mask("".join(p[0] for p in parts[:i + 1]), s).width
        fill.alpha_composite(paint(whole, light, dark).crop((x, 0, end, whole.height)), (x, 0))
        x = end
    if italic:
        whole, fill = shear(whole, italic), shear(fill, italic)
    pad = outline + shadow
    out = Image.new("RGBA", (whole.width + pad * 2, whole.height + pad * 2), CLEAR)
    body = Image.new("L", out.size, 0)     # the letters with their outline: what the shadow and the black take
    body.paste(dilate(whole, outline) if outline else whole, (0, 0))
    if shadow:
        out.paste(BLACK, (shadow, shadow), body)
    out.paste(BLACK, (0, 0), body)
    out.alpha_composite(fill, (outline, outline))
    return out.crop(out.getbbox())


def prect(w, h, r, fill, outline=None, width=1):
    """A rounded rectangle with stepped pixel corners, as the UI's cards; `r` is the staircase per corner."""
    im = Image.new("RGBA", (w, h), CLEAR)
    d = ImageDraw.Draw(im)
    steps = {1: [1], 2: [2, 1], 3: [3, 1, 1], 4: [4, 2, 1, 1], 5: [5, 3, 2, 1, 1], 6: [6, 4, 3, 2, 1, 1]}[r]

    def body(col, inset):
        for y in range(inset, h - inset):
            row = y - inset
            edge = min(row, h - 2 * inset - 1 - row)
            cut = steps[edge] if edge < len(steps) else 0
            d.line([(inset + cut, y), (w - 1 - inset - cut, y)], fill=col)
    if outline:
        body(outline, 0)
        body(fill, width)
    else:
        body(fill, 0)
    return im


def dot_grid(w, h, step, base, dot):
    im = Image.new("RGBA", (w, h), base)
    d = ImageDraw.Draw(im)
    for y in range(1, h, step):
        for x in range(1, w, step):
            d.point((x, y), fill=dot)
    return im


def icon(name):
    return Image.open(os.path.join(ICONS, name + ".png")).convert("RGBA")


def banana(px):
    return Image.open(os.path.join(BANANA, f"banana-{px}.png")).convert("RGBA")


def chip(t, fg, bg, border, k, s, padx=4, pady=3):
    """A label in a stepped box: the box in art pixels (k), the text at s image pixels per font pixel."""
    tl = text(t, fg, s)
    w = -(-(tl.width + 2 * padx * k) // k)
    h = -(-(mask("H", s).height + 2 * pady * k) // k)
    c = up(prect(w, h, 2, bg, border), k)
    c.alpha_composite(tl, ((c.width - tl.width) // 2, pady * k))
    return c


def fit(img, w, h):
    return img.convert("RGBA").resize((w, h), Image.LANCZOS)


def drop(im, d, alpha=150):
    """A hard drop shadow `d` image pixels down and right, as the UI's cards cast."""
    a = im.getchannel("A").point(lambda v: 255 if v > 0 else 0)
    out = Image.new("RGBA", (im.width + d, im.height + d), CLEAR)
    out.paste((0, 0, 0, alpha), (d, d), a)
    out.alpha_composite(im)
    return out


# ---- the clamshell -------------------------------------------------------------------------------------------------

def clamshell(top, bot, sw, sh, k):
    """The handheld open, top panel over bottom: chrome in art pixels scaled by k, the grabs fitted to sw x sh art
    pixels each (sw*k x sh*k image pixels)."""
    body, rim = (34, 33, 40, 255), (70, 66, 76, 255)
    m, hinge = 6, 8
    w, lid = sw + m * 2, sh + m * 2
    art = Image.new("RGBA", (w, lid * 2 + hinge), CLEAR)
    art.alpha_composite(prect(w, lid, 4, body, rim), (0, 0))
    art.alpha_composite(prect(w, lid, 4, body, rim), (0, lid + hinge))
    d = ImageDraw.Draw(art)
    d.rectangle([10, lid - 1, w - 11, lid + hinge], fill=(20, 19, 24, 255))     # the hinge's barrel
    d.rectangle([10, lid + 2, 24, lid + hinge - 3], fill=rim)                    # and its two pins
    d.rectangle([w - 25, lid + 2, w - 11, lid + hinge - 3], fill=rim)
    d.rectangle([w // 2 - 1, lid + 3, w // 2, lid + 4], fill=GREEN)              # the power light
    for y0 in (m - 2, lid + hinge + m - 2):
        d.rectangle([m - 2, y0, m + sw + 1, y0 + sh + 3], fill=BLACK)            # the bezels
    out = up(art, k)
    out.alpha_composite(fit(top, sw * k, sh * k), (m * k, m * k))
    out.alpha_composite(fit(bot, sw * k, sh * k), (m * k, (lid + hinge + m) * k))
    return out


# ---- the logos ----------------------------------------------------------------------------------------------------

def bananads_icon():
    """BananaDS's mark: a DS lying open, the bunch on its top screen, the menu's yellow plate on the bottom one."""
    w, lid, gap = 40, 24, 3
    shell, hi = (60, 58, 68, 255), (96, 92, 104, 255)
    im = Image.new("RGBA", (w, lid * 2 + gap), CLEAR)
    im.alpha_composite(prect(w, lid, 3, shell, BLACK), (0, 0))
    im.alpha_composite(prect(w, lid, 3, shell, BLACK), (0, lid + gap))
    d = ImageDraw.Draw(im)
    d.rectangle([4, lid - 1, w - 5, lid + gap], fill=(30, 29, 34, 255))         # the hinge
    d.line([(4, lid - 1), (w - 5, lid - 1)], fill=BLACK)
    d.line([(4, lid + gap), (w - 5, lid + gap)], fill=BLACK)
    d.line([(4, 1), (w - 5, 1)], fill=hi)
    d.line([(4, lid + gap + 1), (w - 5, lid + gap + 1)], fill=hi)
    sx0, sx1 = 8, w - 9                                                          # the top screen, teal, the bunch
    d.rectangle([sx0 - 1, 3, sx1 + 1, lid - 4], fill=BLACK)
    d.rectangle([sx0, 4, sx1, lid - 5], fill=TEAL_LIGHT)
    b = banana(16)
    b = b.crop(b.getbbox())
    im.alpha_composite(b, ((w - b.width) // 2 + 1, 4 + (lid - 9 - b.height) // 2 + 1))
    by = lid + gap                                                               # the bottom screen and plate
    d.rectangle([sx0 + 2, by + 3, sx1 - 2, by + lid - 4], fill=BLACK)
    d.rectangle([sx0 + 3, by + 4, sx1 - 3, by + lid - 5], fill=PANEL)
    d.rectangle([sx0 + 7, by + 9, sx1 - 7, by + lid - 8], fill=BLACK)
    d.rectangle([sx0 + 8, by + 10, sx1 - 8, by + lid - 9], fill=YELLOW)
    d.line([(sx0 + 8, by + lid - 10), (sx1 - 8, by + lid - 10)], fill=YELLOW_DARK)
    cy = by + lid // 2 - 1
    d.point([(4, cy - 1), (3, cy), (4, cy), (5, cy), (4, cy + 1)], fill=CREAM)                   # the d-pad
    d.point([(w - 5, cy - 2), (w - 3, cy), (w - 7, cy), (w - 5, cy + 2)], fill=CREAM)            # A B X Y
    return im


def bananads_logo(k):
    """The bunch, then BANANA heavy in two yellows with a black outline (as SuperDrastic's DRASTIC) and DS beside it in
    white, the same height, so the two logos read as a pair."""
    w = word([("BANANA", BANANA_LIGHT, BANANA_DARK), ("DS", WHITE, CREAM_DARK)], s=5, shadow=5, outline=3, heavy=True)
    b = banana(32)
    b = b.crop(b.getbbox())
    mark = up(b, -(-w.height * 5 // (4 * b.height)))      # the bunch a bit taller than the word, as SuperDrastic's bolt
    gap = 16
    im = Image.new("RGBA", (mark.width + gap + w.width, max(mark.height, w.height)), CLEAR)
    im.alpha_composite(mark, (0, (im.height - mark.height) // 2))
    im.alpha_composite(w, (mark.width + gap, (im.height - w.height) // 2))
    return up(im, k)


BOLT = ["    #####",
        "   #yyyy#",
        "  #yyyy# ",
        "  #yyy#  ",
        " #yyyy###",
        " #YYYYYY#",
        "###YYYY# ",
        "#YYYYY#  ",
        "###YY#   ",
        "  #Y#    ",
        "  ##     "]


def bolt():
    im = Image.new("RGBA", (9, len(BOLT)), CLEAR)
    for y, row in enumerate(BOLT):
        for x, c in enumerate(row):
            if c != " ":
                im.putpixel((x, y), {"#": BLACK, "y": BANANA_LIGHT, "Y": BANANA_DARK}[c])
    return im


def superdrastic_logo(k):
    """SuperDrastic's mark, after DraStic's red italic wordmark in Banana's pixel look: DRASTIC heavy and slanted in
    two reds with a black outline, SUPER on the menu's yellow START plate, and a bolt for the speed."""
    big = word([("DRASTIC", RED_LIGHT, RED_DARK)], s=5, shadow=5, outline=3, italic=4, heavy=True)
    plate_t = text("SUPER", BLACK, 3)
    pw, ph = plate_t.width + 20, plate_t.height + 14
    plate = Image.new("RGBA", (pw + 4, ph + 4), CLEAR)
    plate.alpha_composite(up(prect(pw // 2, ph // 2, 2, YELLOW, BLACK), 2), (0, 0))
    ImageDraw.Draw(plate).rectangle([4, ph - 8, pw - 6, ph - 5], fill=YELLOW_DARK)
    plate.alpha_composite(plate_t, (10, 6))
    # SUPER sits above DRASTIC, its right edge on DRASTIC's, clear of the letters (it used to cover the C)
    b = up(bolt(), 6)
    top = ph + 2
    w = b.width + 8 + big.width
    h = top + big.height
    im = Image.new("RGBA", (w, h), CLEAR)
    im.alpha_composite(b, (0, top + (big.height - b.height) // 2))
    im.alpha_composite(big, (b.width + 8, top))
    im.alpha_composite(plate, (w - pw - 8, 0))
    return up(im, k)


# ---- the banner ---------------------------------------------------------------------------------------------------

def banner(grabs):
    k = 4
    W, H = 1280, 840
    out = up(dot_grid(W // k, H // k, 3, BG, (25, 24, 31, 255)), k)
    # the handheld, right, on a teal pad (the menu's top panel)
    hs = clamshell(Image.open(f"{grabs}/home-top.png"), Image.open(f"{grabs}/home-bot.png"), 100, 75, k)
    hx, hy = W - hs.width - 84, (H - hs.height) // 2
    pad = up(prect((hs.width + 96) // k, (hs.height + 64) // k, 6, (19, 30, 32, 255), (31, 50, 52, 255)), k)
    out.alpha_composite(pad, (hx - 48, hy - 32))
    out.alpha_composite(drop(hs, 16), (hx, hy))
    # left: the bunch, the wordmark, what it is
    x = 72
    out.alpha_composite(up(banana(32), 4), (x, 92))
    wm = up(word([("BANANA", BANANA_LIGHT, BANANA_DARK), ("OS", WHITE, CREAM_DARK)]), 8)
    out.alpha_composite(wm, (x, 248))
    y = 352
    for line in ("A light Linux for the", "Anbernic RG DS", "and RG DS Plus"):
        out.alpha_composite(text(line, CREAM, 5), (x + 2, y))
        y += 58
    d = ImageDraw.Draw(out)
    d.rectangle([x + 2, y + 6, x + 474, y + 13], fill=(58, 53, 64, 255))
    d.rectangle([x + 2, y + 6, x + 130, y + 13], fill=YELLOW)
    y += 40
    cx = x + 2
    for t, fg, bg, br in (("ALPHA 0.4.2", BLACK, YELLOW, YELLOW_DARK), ("RG DS", CREAM, CARD, LINE),
                          ("RG DS PLUS", CREAM, CARD, LINE)):
        c = chip(t, fg, bg, br, 4, 3)
        out.alpha_composite(c, (cx, y))
        cx += c.width + 14
    y += 92
    for line in ("Boots into its own menu in seconds.", "Two DS emulators and RetroArch.",
                 "Every cycle goes to the game."):
        out.alpha_composite(text(line, GREY, 3), (x + 2, y))
        y += 36
    return out


# ---- section headers ----------------------------------------------------------------------------------------------

def header(title, icon_name, num, total, sub):
    """A section's title bar, like the menu's: its icon on the yellow START plate, the title, a rule, the count, on an
    opaque panel so it reads on GitHub's light pages too."""
    W, H, k = 1280, 128, 4
    out = up(prect(W // k, H // k, 4, PANEL, LINE), k)
    grid = up(dot_grid(W // k - 2, H // k - 2, 3, PANEL, DOT), k)
    out.paste(grid, (k, k), up(prect(W // k - 2, H // k - 2, 3, WHITE), k))
    plate = prect(46, 46, 4, YELLOW, BLACK)
    ImageDraw.Draw(plate).rectangle([3, 36, 42, 42], fill=YELLOW_DARK)
    plate.alpha_composite(prect(38, 38, 3, CARD, BLACK), (4, 2))
    plate.alpha_composite(banana(32) if icon_name == "banana" else icon(icon_name), (7, 5))
    out.alpha_composite(drop(up(plate, 2), 6), (16, 14))
    x = 136
    t = word([(title.upper(), CREAM, CREAM_DARK)], s=5, shadow=4)
    out.alpha_composite(t, (x, 20))
    out.alpha_composite(text(sub, GREY, 3), (x + 2, 20 + t.height + 12))
    n = text(f"{num:02d} / {total:02d}", GREY, 3)
    d = ImageDraw.Draw(out)
    rx = x + t.width + 28
    ry = 20 + mask("H", 5).height // 2
    d.rectangle([rx, ry, W - n.width - 48, ry + 3], fill=LINE)
    d.rectangle([rx, ry, rx + 48, ry + 3], fill=YELLOW)
    out.alpha_composite(n, (W - n.width - 26, ry - n.height // 2 + 2))
    return out


# ---- a screenshot pair --------------------------------------------------------------------------------------------

def shot(grabs, name):
    return drop(clamshell(Image.open(f"{grabs}/{name}-top.png"), Image.open(f"{grabs}/{name}-bot.png"), 128, 96, 4), 8)


# ---- the two emulators --------------------------------------------------------------------------------------------

def emulator_card(logo, tag, lines, chips, accent):
    k = 4
    W, H = 620, 560
    out = up(prect(W // k, H // k, 5, CARD, CREAM), k)
    head = 196
    top = up(dot_grid(W // k - 2, head // k - 1, 3, PANEL, DOT), k)
    tm = up(prect(W // k - 2, head // k, 4, WHITE), k).getchannel("A").crop((0, 0, top.width, top.height))
    out.paste(top, (k, k), tm)
    d = ImageDraw.Draw(out)
    d.rectangle([k, head, W - k - 1, head + k - 1], fill=LINE)
    lg = logo
    out.alpha_composite(lg, ((W - lg.width) // 2, (head - lg.height) // 2 + 2))
    y = head + 30
    out.alpha_composite(text(tag.upper(), accent, 3), (36, y))
    y += 54
    for line in lines:
        d.rectangle([36, y + 6, 45, y + 15], fill=accent)
        for i, part in enumerate(line):
            out.alpha_composite(text(part, CREAM if i == 0 else GREY, 3), (62, y))
            y += 32
        y += 12
    cx = 36
    for t in chips:
        c = chip(t, CREAM, PANEL, LINE, 4, 3)
        out.alpha_composite(c, (cx, H - c.height - 32))
        cx += c.width + 12
    return out


def emulators():
    left = emulator_card(superdrastic_logo(1), "DraStic, built for both screens", [
        ("2x rendering with",  "Gengis Engine 3D"),
        ("Per-game CPU tuning",),
        ("Save states with previews", "and shader settings"),
    ], ["2X", "STATES", "SHADERS"], RED_LIGHT)
    right = emulator_card(bananads_logo(1), "melonDS, more ways to play", [
        ("RetroAchievements and", "Nintendo WFC online play"),
        ("Real microphone support",),
        ("Cheats and fast-forward",),
    ], ["ACHIEVEMENTS", "ONLINE", "MIC"], YELLOW)
    W = 1280
    out = Image.new("RGBA", (W, left.height + 16 + 60), CLEAR)
    out.alpha_composite(drop(left, 12), (4, 4))
    out.alpha_composite(drop(right, 12), (W - right.width - 16, 4))
    hint = text("A default in Settings > Nintendo DS, or X in the library for one game", GREY, 3)
    out.alpha_composite(hint, ((W - hint.width) // 2, out.height - hint.height - 6))
    return out


# ---- videos about it ----------------------------------------------------------------------------------------------

PLAY = ["##        ",
        "#Y##      ",
        "#YYY##    ",
        "#YYYYY##  ",
        "#YYYYYYY##",
        "#YYYYYYY##",
        "#YYYYY##  ",
        "#YYY##    ",
        "#Y##      ",
        "##        "]


def video_card(thumb, channel, title):
    """A video as a card: its thumbnail in a bezel with the menu's play plate on it, the channel on the yellow plate,
    the title, and where it plays."""
    k = 4
    W, H = 620, 588
    out = up(prect(W // k, H // k, 5, CARD, CREAM), k)
    tw, th = W - 2 * 28, (W - 2 * 28) * 9 // 16
    d = ImageDraw.Draw(out)
    d.rectangle([24, 24, 24 + tw + 7, 24 + th + 7], fill=BLACK)
    out.alpha_composite(fit(thumb, tw, th), (28, 28))
    plate = prect(26, 22, 3, YELLOW, BLACK)
    ImageDraw.Draw(plate).rectangle([2, 17, 23, 19], fill=YELLOW_DARK)
    tri = Image.new("RGBA", (10, 10), CLEAR)
    for y, row in enumerate(PLAY):
        for x, c in enumerate(row):
            if c != " ":
                tri.putpixel((x, y), BLACK if c == "#" else (36, 32, 28, 255))
    plate.alpha_composite(tri, (9, 5))
    plate = drop(up(plate, k), 8)
    out.alpha_composite(plate, (28 + (tw - plate.width) // 2, 28 + (th - plate.height) // 2))
    y = 28 + th + 30
    c = chip(channel.upper(), BLACK, YELLOW, YELLOW_DARK, 4, 3)
    out.alpha_composite(c, (28, y))
    y += c.height + 20
    words, line, lines = title.split(), "", []
    for w in words:
        if text(line + " " + w if line else w, CREAM, 3).width > W - 60:
            lines.append(line)
            line = w
        else:
            line = line + " " + w if line else w
    lines.append(line)
    for ln in lines:
        out.alpha_composite(text(ln, CREAM, 3), (30, y))
        y += 34
    out.alpha_composite(text("WATCH ON YOUTUBE >", GREY, 3), (30, H - 54))
    return drop(out, 12)


VIDEOS = [  # (YouTube id, channel, title)
    ("yW6LavgyhL4", "Retro Game Corps", "Custom Firmware on the Anbernic RG DS Plus"),
    ("KPENdDlkI5M", "forthenext", "ANBERNIC RG DS Plus, BananaOS and your games tested"),
]

# ---- the numbers, as the home screen shows a system's -------------------------------------------------------------

STATS = [  # (icon, number, unit, label)
    ("gear", "3.9", "S", "RG DS TO MENU"),
    ("gear", "5.9", "S", "RG DS PLUS TO MENU"),
    ("nds", "2", "", "DS EMULATORS"),
]


def stats():
    k = 4
    W, H, n = 1280, 252, len(STATS)
    gap = 20
    tw = (W - gap * (n - 1) - 12) // n
    out = Image.new("RGBA", (W, H), CLEAR)
    for i, (ic, num, unit, label) in enumerate(STATS):
        t = up(prect(tw // k, (H - 12) // k, 5, TEAL, CREAM), k)
        g = up(dot_grid(tw // k - 2, (H - 12) // k - 2, 3, TEAL, (43, 76, 79, 255)), k)
        t.paste(g, (k, k), up(prect(tw // k - 2, (H - 12) // k - 2, 4, WHITE), k))
        plate = prect(38, 38, 3, CARD, BLACK)
        plate.alpha_composite(icon(ic), (3, 3))
        plate = drop(up(plate, 2), 6)
        t.alpha_composite(plate, ((t.width - plate.width) // 2, 18))
        nm = word([(num, WHITE, CREAM_DARK)], s=6, shadow=4)
        un = text(unit, CREAM, 4) if unit else None
        w = nm.width + (un.width + 8 if un else 0)
        x = (t.width - w) // 2
        y = 18 + plate.height + 14
        t.alpha_composite(nm, (x, y))
        if un:
            t.alpha_composite(un, (x + nm.width + 8, y + nm.height - un.height - 4))
        lb = text(label, CREAM_DARK, 3)
        t.alpha_composite(lb, ((t.width - lb.width) // 2, y + nm.height + 14))
        out.alpha_composite(drop(t, 10), (i * (tw + gap), 0))
    return out


# ---- ROCKNIXDS's front page: where its work went -----------------------------------------------------------------

def moved(grabs):
    """For JorreFog/ROCKNIXDS's README: its development continues as BananaOS. A card with the handheld running
    BananaOS, the wordmark, and the menu's yellow plate as the button."""
    k = 4
    W, H = 1280, 448
    out = up(prect(W // k, H // k, 6, PANEL, CREAM), k)
    out.paste(up(dot_grid(W // k - 2, H // k - 2, 3, PANEL, DOT), k), (k, k),
              up(prect(W // k - 2, H // k - 2, 5, WHITE), k))
    hs = clamshell(Image.open(f"{grabs}/home-top.png"), Image.open(f"{grabs}/home-bot.png"), 64, 48, 3)
    out.alpha_composite(drop(hs, 8), (40, (H - hs.height) // 2 - 4))
    x = 40 + hs.width + 56
    out.alpha_composite(text("DEVELOPMENT HAS MOVED TO", YELLOW, 3), (x + 2, 40))
    out.alpha_composite(up(banana(32), 2), (x, 84))
    wm = up(word([("BANANA", BANANA_LIGHT, BANANA_DARK), ("OS", WHITE, CREAM_DARK)]), 6)
    out.alpha_composite(wm, (x + 84, 92))
    y = 172
    for line in ("ROCKNIXDS's work goes on as BananaOS, a whole", "Linux of its own for the RG DS and RG DS Plus:",
                 "the same menu, two DS emulators, a fast boot.", "ROCKNIXDS stays here for those who use it."):
        out.alpha_composite(text(line, CREAM_DARK, 3), (x + 2, y))
        y += 34
    plate_t = text("GO TO BANANAOS >", BLACK, 3)
    pw, ph = plate_t.width + 48, plate_t.height + 32
    plate = up(prect(pw // k + 1, ph // k + 1, 3, YELLOW, BLACK), k)
    ImageDraw.Draw(plate).rectangle([k, plate.height - 3 * k, plate.width - k - 1, plate.height - k - 1],
                                    fill=YELLOW_DARK)
    plate.alpha_composite(plate_t, ((plate.width - plate_t.width) // 2, 14))
    out.alpha_composite(drop(plate, 6), (x, H - plate.height - 40))
    return out


# ---- the flasher: jesussaves-ai's card writer ---------------------------------------------------------------------

def sd_card():
    """A microSD-ish card, the bunch on its yellow label: what the flasher writes."""
    w, h = 26, 32
    body, edge = (52, 50, 60, 255), (86, 82, 94, 255)
    im = Image.new("RGBA", (w, h), CLEAR)
    d = ImageDraw.Draw(im)
    d.polygon([(0, 0), (w - 8, 0), (w - 1, 7), (w - 1, h - 1), (0, h - 1)], fill=BLACK)
    d.polygon([(1, 1), (w - 9, 1), (w - 2, 8), (w - 2, h - 2), (1, h - 2)], fill=body)
    d.line([(2, 2), (w - 10, 2)], fill=edge)
    for i in range(5):                                                            # the contacts
        d.rectangle([3 + i * 3, 3, 4 + i * 3, 8], fill=YELLOW_DARK)
    d.rectangle([3, 12, w - 4, h - 4], fill=BLACK)                                # the label
    d.rectangle([4, 13, w - 5, h - 5], fill=YELLOW)
    d.line([(4, h - 6), (w - 5, h - 6)], fill=YELLOW_DARK)
    b = banana(16)
    b = b.crop(b.getbbox())
    im.alpha_composite(b, ((w - b.width) // 2, 13 + (h - 18 - b.height) // 2))
    return im


def window(shot, sw, k):
    """The flasher's own window, in a pixel frame with a title bar, the screenshot fitted to sw image pixels wide."""
    sh = shot.height * sw // shot.width
    bar = 9
    w, h = sw // k + 4, sh // k + bar + 4
    art = prect(w, h, 3, (34, 33, 40, 255), (70, 66, 76, 255))
    d = ImageDraw.Draw(art)
    d.rectangle([2, bar + 1, w - 3, h - 3], fill=BLACK)
    for i, c in enumerate((RED_LIGHT, YELLOW, GREEN)):                            # the window's three buttons
        d.rectangle([w - 8 - i * 5, 3, w - 6 - i * 5, 5], fill=c)
    out = up(art, k)
    b = banana(16)
    out.alpha_composite(b, (3 * k, (bar * k - b.height) // 2 + 2))
    t = text("BananaLinuxFlasher.exe", CREAM_DARK, 2)
    out.alpha_composite(t, (3 * k + b.width + 10, (bar * k - t.height) // 2 + 2))
    out.alpha_composite(fit(shot, sw, sh), (2 * k + (w * k - 4 * k - sw) // 2, (bar + 2) * k))
    return out


def flasher(shot):
    """jesussaves-ai's BananaLinux Flasher, featured: its window, the card it writes, its three steps, and the yellow
    plate as the download button."""
    k = 4
    W, H = 1280, 640
    out = up(prect(W // k, H // k, 6, PANEL, CREAM), k)
    out.paste(up(dot_grid(W // k - 2, H // k - 2, 3, PANEL, DOT), k), (k, k),
              up(prect(W // k - 2, H // k - 2, 5, WHITE), k))
    shot = shot.crop((0, 0, shot.width, 612))                                     # the header and its three steps
    win = drop(window(shot, 540, k), 12)
    wx, wy = 40, (H - win.height) // 2
    out.alpha_composite(win, (wx, wy))
    card = drop(up(sd_card(), k), 10)
    out.alpha_composite(card, (wx + win.width - card.width + 28, wy + win.height - card.height + 30))

    x = wx + win.width + 56
    y = 44
    out.alpha_composite(text("THE EASY WAY ONTO A CARD", YELLOW, 3), (x + 2, y))
    y += 44
    wm = up(word([("BANANA", BANANA_LIGHT, BANANA_DARK), ("LINUX", WHITE, CREAM_DARK)]), 5)
    out.alpha_composite(wm, (x, y))
    y += wm.height + 10
    fl = up(word([("FLASHER", CREAM, CREAM_DARK)]), 5)
    out.alpha_composite(fl, (x, y))
    by = chip("BY JESUSSAVES-AI", CREAM, TEAL, TEAL_LIGHT, 4, 3)
    out.alpha_composite(by, (x + fl.width + 18, y + fl.height - by.height))
    y += fl.height + 30
    for n, line in (("1", "Pick your handheld"), ("2", "Pick the microSD card"), ("3", "Download & Flash")):
        num = chip(n, BLACK, YELLOW, YELLOW_DARK, 4, 3, padx=3, pady=2)
        out.alpha_composite(num, (x, y))
        t = text(line, CREAM, 3)
        out.alpha_composite(t, (x + num.width + 16, y + (num.height - t.height) // 2))
        y += num.height + 12
    y += 10
    for line in ("Newest release, SHA-256 checked,", "written straight to the card."):
        out.alpha_composite(text(line, GREY, 3), (x + 2, y))
        y += 32
    y += 22
    cx = x
    for t in ("WINDOWS .EXE", "LINUX", "MACOS"):
        c = chip(t, CREAM, CARD, LINE, 4, 3)
        out.alpha_composite(c, (cx, y))
        cx += c.width + 12
    plate_t = text("GET THE FLASHER >", BLACK, 3)
    pw, ph = plate_t.width + 48, plate_t.height + 32
    plate = up(prect(pw // k + 1, ph // k + 1, 3, YELLOW, BLACK), k)
    ImageDraw.Draw(plate).rectangle([k, plate.height - 3 * k, plate.width - k - 1, plate.height - k - 1],
                                    fill=YELLOW_DARK)
    plate.alpha_composite(plate_t, ((plate.width - plate_t.width) // 2, 14))
    out.alpha_composite(drop(plate, 6), (x, H - plate.height - 44))
    return out


SHOTS = ["home", "library", "options", "settings", "nds", "zh-home", "zh-settings", "game", "game-menu", "store",
         "light", "carts"]
HEADERS = [  # (file, title, icon, subtitle)
    ("what", "What it is", "banana", "Lightweight Linux for two handhelds"),
    ("install", "Get it", "cart", "Install, update and move your saves"),
    ("tour", "A tour", "picture", "Captured on an RG DS Plus running 0.4.2"),
    ("emulators", "Nintendo DS", "nds", "Two emulators. Your choice."),
    ("systems", "More to play", "gamepad", "More systems, apps and ports"),
    ("hardware", "Built for it", "handheld", "Built for two screens and a lid"),
    ("status", "Status", "tools", "Known issues and what comes next"),
    ("build", "Build", "computer", "Build from source and work on the menu"),
    ("videos", "Seen on YouTube", "video", "See BananaOS in action"),
    ("credits", "Credits", "fav", "Built on community work"),
]

if __name__ == "__main__":
    grabs, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    banner(grabs).save(f"{out}/banner.png", optimize=True)
    superdrastic_logo(2).save(f"{out}/superdrastic-logo.png", optimize=True)
    bananads_logo(2).save(f"{out}/bananads-logo.png", optimize=True)
    emulators().save(f"{out}/emulators.png", optimize=True)
    stats().save(f"{out}/stats.png", optimize=True)
    moved(grabs).save(f"{out}/rocknixds-moved.png", optimize=True)
    for i, (f, t, ic, sub) in enumerate(HEADERS):
        header(t, ic, i + 1, len(HEADERS), sub).save(f"{out}/h-{f}.png", optimize=True)
    for s in SHOTS:
        shot(grabs, s).save(f"{out}/shot-{s}.png", optimize=True)
    if len(sys.argv) > 3:
        for vid, channel, title in VIDEOS:
            video_card(Image.open(f"{sys.argv[3]}/{vid}.jpg"), channel, title).save(f"{out}/video-{vid}.png",
                                                                                    optimize=True)
    if len(sys.argv) > 4:
        flasher(Image.open(sys.argv[4])).save(f"{out}/flasher.png", optimize=True)

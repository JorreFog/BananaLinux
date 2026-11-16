#!/usr/bin/env python3
"""A fake card for trying Randomizer on a computer.

Writes a card with four systems, a few ROMs (two DS games with real DS banner icons),
gamelist.xml names, media/box covers and the menu's playstats. Nothing copyrighted: the
titles and the covers are made up for the previews.

    python tools/make-testdata.py /tmp/randomizer-card
    ./randomizer-host --root /tmp/randomizer-card --systems /tmp/randomizer-card/systems.conf \
        --roms /tmp/randomizer-card/roms --assets ../../../ui/assets/rnds \
        --smoke /tmp/rnd --setting box
    ./randomizer-host ... --setting icon --smoke /tmp/rnd-icon
"""
import math
import os
import struct
import sys

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", "..", ".."))
FONT = os.path.join(ROOT, "ui", "assets", "rnds", "fonts", "PixelifySans-Medium.ttf")

SYSTEMS = """# a made-up card for Randomizer's previews
[nds]
name = Nintendo DS
maker = Nintendo
ext = nds zip
run = /usr/libexec/banana/run-nds %ROM%
always = yes

[gba]
name = Game Boy Advance
maker = Nintendo
ext = gba zip
run = /usr/libexec/banana/run-retroarch gpsp %ROM%

[snes]
name = Super Nintendo
maker = Nintendo
ext = sfc smc
run = /usr/libexec/banana/run-retroarch snes9x %ROM%

[files]
name = Files
maker = Banana
folder = /usr/share/banana/apps/files
ext = app
run = banana:files
always = yes
"""

# title, system, file name, box colours (paper, ink, accent), art kind
GAMES = [
    ("Peel Quest", "nds", "Peel Quest (USA).nds", ("#f2d46b", "#2a2118", "#e8613c"), "banana"),
    ("Moonlit Errand", "nds", "Moonlit Errand.nds", ("#28334f", "#f4ead7", "#7cc0f2"), "moon"),
    ("Orchard Tactics", "gba", "Orchard Tactics (Europe).gba", ("#8b6ad8", "#f7f1e3", "#ffcf5c"), "grid"),
    ("Block Bay", "gba", "Block Bay.gba", ("#2eae62", "#f7f1e3", "#ff8a5c"), "blocks"),
    ("Monkey Puzzle League", "snes", "Monkey Puzzle League.sfc", ("#e25a5a", "#fff6dc", "#3d9ec4"), "faces"),
    ("Sunbeam Saga", "snes", "Sunbeam Saga (Japan).sfc", ("#e8a33d", "#2a2118", "#fff6dc"), "sun"),
]

STATS = {
    "nds/Peel Quest (USA).nds": (12, 4 * 3600 + 18 * 60, -2),
    "gba/Orchard Tactics (Europe).gba": (7, 2 * 3600 + 5 * 60, -9),
    "snes/Sunbeam Saga (Japan).sfc": (3, 45 * 60, -31),
}


def font(size):
    return ImageFont.truetype(FONT, size)


def cover(title, paper, ink, accent, kind, path, w=320, h=480):
    im = Image.new("RGB", (w, h), paper)
    d = ImageDraw.Draw(im)
    # a paper grain: a few darker rectangles, very quiet
    for y in range(0, h, 16):
        d.rectangle([0, y, w, y + 8], fill=tuple(max(0, c - 4) for c in im.getpixel((0, y))))
    d.rectangle([0, 0, w, 34], fill=ink)
    d.text((12, 8), "BANANA ARCADE", font=font(16), fill=paper)
    d.rectangle([0, h - 28, w, h], fill=ink)
    d.text((12, h - 25), "ONE PLAYER · SAVE SOMEWHERE", font=font(13), fill=paper)

    # the picture: a simple, chunky motif in the accent colour
    cx, cy = w // 2, 232
    if kind == "banana":
        d.ellipse([cx - 68, cy - 86, cx + 68, cy + 86], fill=accent)
        d.ellipse([cx - 50, cy - 68, cx + 46, cy + 68], fill=paper)
        d.arc([cx - 58, cy - 76, cx + 58, cy + 58], 205, 325, fill=ink, width=8)
    elif kind == "moon":
        d.ellipse([cx - 76, cy - 76, cx + 76, cy + 76], fill=accent)
        d.ellipse([cx - 42, cy - 88, cx + 92, cy + 50], fill=paper)
        for i, (sx, sy, r) in enumerate([(-90, -100, 4), (92, -60, 5), (78, 92, 3), (-96, 64, 6)]):
            d.ellipse([cx + sx - r, cy + sy - r, cx + sx + r, cy + sy + r], fill=ink)
    elif kind == "grid":
        for gy in range(3):
            for gx in range(3):
                x = cx - 84 + gx * 58
                y = cy - 84 + gy * 58
                d.rounded_rectangle([x, y, x + 46, y + 46], radius=12, fill=accent, outline=ink, width=5)
                d.ellipse([x + 15, y + 13, x + 31, y + 29], fill=ink)
    elif kind == "blocks":
        shapes = [(-88, -60, 0), (-10, -60, 1), (-88, 18, 1), (-10, 18, 2), (66, -60, 2), (66, 18, 0)]
        for x, y, s in shapes:
            x += cx
            y += cy
            d.rectangle([x, y, x + 64, y + 64], fill=accent if s != 1 else ink, outline=paper, width=4)
    elif kind == "faces":
        for i, x in enumerate((-92, 0, 92)):
            y = cy - 40 + (18 if i == 1 else 0)
            d.ellipse([cx + x - 40, y - 40, cx + x + 40, y + 40], fill=accent, outline=ink, width=6)
            d.ellipse([cx + x - 22, y - 12, cx + x - 10, y], fill=ink)
            d.ellipse([cx + x + 10, y - 12, cx + x + 22, y], fill=ink)
            d.arc([cx + x - 20, y + 2, cx + x + 20, y + 30], 20, 160, fill=ink, width=5)
    elif kind == "sun":
        for i in range(12):
            a = i * math.pi / 6
            x = cx + math.cos(a) * 100
            y = cy + math.sin(a) * 100
            d.line([cx, cy, x, y], fill=accent, width=9)
        d.ellipse([cx - 62, cy - 62, cx + 62, cy + 62], fill=accent, outline=ink, width=7)

    # the title, wrapped
    lines, line = [], ""
    for word in title.replace("-", "- ").split():
        t = (line + " " + word).strip()
        if d.textlength(t, font=font(36)) <= w - 36:
            line = t
        else:
            if line:
                lines.append(line)
            line = word
    if line:
        lines.append(line)
    y = h - 96 - len(lines) * 38
    for line in lines:
        tw = d.textlength(line, font=font(36))
        d.text(((w - tw) / 2, y), line, font=font(36), fill=ink)
        y += 38
    im.save(path)


def ds_icon(seed, accent, path):
    """A 32x32 DS banner icon: a little cartridge with a star, in the icon pack's style."""
    # 0 transparent, 1 black, 2 cream, 3 accent, 4 accent light, 5 dark ink, 6 red, 7 blue
    accent = tuple(int(accent[i:i + 2], 16) for i in (1, 3, 5))
    pal = [
        (0, 0, 0), (0x17, 0x16, 0x1c), (0xf1, 0xed, 0xe2), accent, (0xff, 0xe6, 0x8a),
        (0x4e, 0x2e, 0x12), (0xe8, 0x57, 0x4d), (0x4a, 0x9f, 0xe0),
    ]
    im = Image.new("P", (32, 32), 0)
    flat = []
    for c in pal:
        flat.extend(c)
    im.putpalette(flat)
    d = ImageDraw.Draw(im)
    # cartridge body
    d.rectangle([5, 7, 26, 24], fill=1)
    d.rectangle([6, 8, 25, 22], fill=2)
    d.rectangle([8, 9, 23, 16], fill=3)
    d.rectangle([8, 9, 23, 12], fill=4)
    # label: a star, or a moon for the second game
    if seed % 2 == 0:
        pts = [(15, 17), (17, 20), (21, 20), (18, 22), (19, 26), (15, 24), (11, 26), (12, 22), (9, 20), (13, 20)]
        d.polygon(pts, fill=5)
        d.rectangle([14, 20, 16, 22], fill=4)
    else:
        d.ellipse([9, 18, 19, 28], fill=5)
        d.ellipse([12, 16, 22, 26], fill=2)
    # contact strip
    for x in range(7, 26, 3):
        d.rectangle([x, 23, x + 2, 24], fill=5)
    out = Image.new("RGBA", (32, 32), (0, 0, 0, 0))
    src = im.load()
    dst = out.load()
    for y in range(32):
        for x in range(32):
            i = src[x, y]
            if i:
                dst[x, y] = pal[i] + (255,)
    out.save(path)


def write_nds(path, title, icon_path):
    icon = Image.open(icon_path).convert("RGBA")
    icon.load()
    tiles = bytearray(512)
    pal = bytearray(32)
    # the image's exact colours, at most 15 of them + transparent
    colours = []
    for y in range(32):
        for x in range(32):
            c = icon.getpixel((x, y))
            if c[3] == 0:
                continue
            if c[:3] not in colours:
                colours.append(c[:3])
    colours = colours[:15]
    for i, c in enumerate(colours):
        r, g, b = c
        v = ((r >> 3) & 31) | (((g >> 3) & 31) << 5) | (((b >> 3) & 31) << 10)
        struct.pack_into("<H", pal, 2 * (i + 1), v)
    for y in range(32):
        for x in range(32):
            c = icon.getpixel((x, y))
            idx = 0
            if c[3]:
                idx = colours.index(c[:3]) + 1 if c[:3] in colours else 1
            t, tx, ty = (y // 8) * 4 + x // 8, x % 8, y % 8
            pos = t * 32 + ty * 4 + tx // 2
            if tx % 2 == 0:
                tiles[pos] = (tiles[pos] & 0xF0) | idx
            else:
                tiles[pos] = (tiles[pos] & 0x0F) | (idx << 4)
    banner = bytearray(0x240)
    struct.pack_into("<H", banner, 0, 1)  # version 1
    banner[0x20:0x20 + 512] = tiles
    banner[0x220:0x220 + 32] = pal
    header = bytearray(0x200)
    struct.pack_into("<I", header, 0x68, 0x200)
    header[0:12] = b"RANDOMTEST\0\0"
    blob = bytes(header) + bytes(banner) + b"\0" * (4096 - 0x200 - 0x240)
    open(path, "wb").write(blob)


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else "/tmp/randomizer-card"
    icon_mode = "--icon" in sys.argv[2:]
    os.makedirs(os.path.join(root, "banana"), exist_ok=True)
    os.makedirs(os.path.join(root, "store"), exist_ok=True)
    os.makedirs(os.path.join(root, "roms"), exist_ok=True)
    open(os.path.join(root, "systems.conf"), "w", encoding="utf-8").write(SYSTEMS)
    open(os.path.join(root, "banana", "settings.conf"), "w", encoding="utf-8").write(
        "accent = yellow\nclock = 24h\nds_cart_art = %s\n" % ("icon" if icon_mode else "box")
    )

    gl = ['<?xml version="1.0"?>', "<gameList>"]
    for idx, (title, sysid, fname, (paper, ink, accent), kind) in enumerate(GAMES):
        d = os.path.join(root, "roms", sysid)
        os.makedirs(d, exist_ok=True)
        os.makedirs(os.path.join(d, "media", "box"), exist_ok=True)
        stem = os.path.splitext(fname)[0]
        box = os.path.join(d, "media", "box", stem + ".png")
        cover(title, paper, ink, accent, kind, box)
        path = os.path.join(d, fname)
        if fname.endswith(".nds"):
            icon = os.path.join(d, "media", "icon-%d.png" % idx)
            ds_icon(idx, accent, icon)
            write_nds(path, title, icon)
        else:
            open(path, "wb").write(b"a made-up ROM for the previews\n")
        if sysid == "nds":
            gl.append("  <game>")
            gl.append("    <path>./%s</path>" % fname.replace("&", "&amp;"))
            gl.append("    <name>%s</name>" % title)
            gl.append("    <genre>%s</genre>" % ("Puzzle" if idx == 0 else "Adventure"))
            gl.append("    <releasedate>2024%s01T000000</releasedate>" % ("03" if idx == 0 else "10"))
            gl.append("    <image>./media/box/%s.png</image>" % stem)
            gl.append("  </game>")
    gl.append("</gameList>")
    open(os.path.join(root, "roms", "nds", "gamelist.xml"), "w", encoding="utf-8").write("\n".join(gl) + "\n")

    now = int(__import__("time").time())
    with open(os.path.join(root, "banana", "playstats.tsv"), "w", encoding="utf-8") as f:
        for key, (plays, secs, days) in sorted(STATS.items()):
            f.write("%s\t%d\t%d\t%d\n" % (key, plays, secs, now + days * 86400))
    print("wrote", root, "with", len(GAMES), "games", "(DS cartridges: %s)" % ("icon" if icon_mode else "box"))


if __name__ == "__main__":
    main()

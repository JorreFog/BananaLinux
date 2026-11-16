#!/usr/bin/env python3
"""A small, dependency-free card for Randomizer's tests.

No Pillow and no fonts: a pure-Python PNG writer and a DS banner give the app a cover and a
32x32 icon to decode, so the test runs anywhere Python 3 does.

    python tests/make-card.py <root>
"""
import os
import struct
import sys
import zlib


def png(path, w, h, pixels):
    raw = b"".join(b"\x00" + bytes(pixels[y]) for y in range(h))

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    blob = b"\x89PNG\r\n\x1a\n"
    blob += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
    blob += chunk(b"IDAT", zlib.compress(raw, 9))
    blob += chunk(b"IEND", b"")
    open(path, "wb").write(blob)


def cover(path, c1, c2, c3):
    w, h = 40, 60
    rows = []
    for y in range(h):
        row = bytearray()
        for x in range(w):
            if y < 6:
                c = c1
            elif y > h - 8:
                c = c2
            elif (x // 6 + y // 6) % 3 == 0:
                c = c3
            else:
                c = c1
            row.extend(c)
        rows.append(row)
    png(path, w, h, rows)


def nds(path):
    """A DS ROM with a valid banner: a diagonal-striped 32x32 icon."""
    tiles = bytearray(512)
    for t in range(16):
        for y in range(8):
            for x in range(8):
                # two-colour diagonal stripes, plus a border
                inside = 1 < (t % 4) * 8 + x < 30 and 1 < (t // 4) * 8 + y < 30
                v = 3 if inside and ((x + y + t) % 5 < 2) else (1 if inside else 0)
                pos = t * 32 + y * 4 + x // 2
                if x % 2 == 0:
                    tiles[pos] = (tiles[pos] & 0xF0) | v
                else:
                    tiles[pos] = (tiles[pos] & 0x0F) | (v << 4)
    pal = bytearray(32)
    # index 0 transparent, 1 dark, 3 accent (BGR555)
    for i, (r, g, b) in ((1, (0x17, 0x16, 0x1c)), (3, (0xf0, 0xb4, 0x29))):
        v = (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10)
        struct.pack_into("<H", pal, 2 * i, v)
    banner = bytearray(0x240)
    struct.pack_into("<H", banner, 0, 1)
    banner[0x20:0x220] = tiles
    banner[0x220:0x240] = pal
    header = bytearray(0x200)
    struct.pack_into("<I", header, 0x68, 0x200)
    header[0:12] = b"RANDOMTEST\0\0"
    open(path, "wb").write(bytes(header) + bytes(banner) + b"\0" * (4096 - 0x200 - 0x240))


def main():
    root = sys.argv[1]
    for d in ("banana", "store", "roms/gba/media/box", "roms/nds/media/box", "roms/gb", "roms/snes",
              "roms/tools", "usr/share/banana/apps/files"):
        os.makedirs(os.path.join(root, d), exist_ok=True)

    open(os.path.join(root, "systems.conf"), "w").write("""[gba]
name = Game Boy Advance
maker = Nintendo
ext = gba
run = /usr/libexec/banana/run-retroarch gpsp %ROM%

[nds]
name = Nintendo DS
maker = Nintendo
ext = nds
run = /usr/libexec/banana/run-nds %ROM%

[gb]
name = Game Boy
maker = Nintendo
ext = gb
run = /usr/libexec/banana/run-retroarch gambatte %ROM%

[snes]
name = Super Nintendo
maker = Nintendo
ext = sfc
run = /usr/libexec/banana/run-retroarch snes9x %ROM%

[tools]
name = Tools
maker = Banana
ext = app
run = /usr/libexec/banana/run-tool %ROM%

[files]
name = Files
maker = Banana
folder = /usr/share/banana/apps/files
ext = app
run = banana:files

[storeapps]
name = Apps
maker = Store
folder = /data/store/tiles
ext = sh
run = /usr/libexec/banana/run-store-app %ROM%
""")
    open(os.path.join(root, "banana", "settings.conf"), "w").write(
        "accent = blue\nclock = 12h\nds_cart_art = box\nshelf.snes = hide\n")
    open(os.path.join(root, "store", "hidden-systems"), "w").write("gb\n")

    from time import time
    now = int(time())
    # a game with spaces and parentheses in its path, and one whose name comes from gamelist.xml
    open(os.path.join(root, "roms", "gba", "Alpha Quest (USA).gba"), "wb").write(b"fake")
    nds(os.path.join(root, "roms", "nds", "Beta Space.nds"))
    open(os.path.join(root, "roms", "gb", "Gamma.gb"), "wb").write(b"fake")
    open(os.path.join(root, "roms", "snes", "Delta.sfc"), "wb").write(b"fake")
    open(os.path.join(root, "roms", "tools", "Widget.app"), "wb").write(b"not a game")
    cover(os.path.join(root, "roms", "gba", "media", "box", "Alpha Quest (USA).png"),
          (0x4a, 0x9f, 0xe0, 255), (0x0f, 0x3f, 0x66, 255), (0xff, 0xe6, 0x8a, 255))
    cover(os.path.join(root, "roms", "nds", "media", "box", "Beta Space.png"),
          (0xa0, 0x7a, 0xe6, 255), (0x43, 0x26, 0x6e, 255), (0xff, 0xf6, 0xdc, 255))
    open(os.path.join(root, "roms", "nds", "gamelist.xml"), "w").write("""<?xml version="1.0"?>
<gameList>
  <game>
    <path>./Beta Space.nds</path>
    <name>Beta Space Deluxe</name>
    <genre>Adventure</genre>
    <releasedate>20240301T000000</releasedate>
    <image>./media/box/Beta Space.png</image>
  </game>
</gameList>
""")
    open(os.path.join(root, "banana", "playstats.tsv"), "w").write(
        "nds/Beta Space.nds\t4\t3600\t%d\n" % (now - 86400))
    print("wrote", root)


if __name__ == "__main__":
    main()

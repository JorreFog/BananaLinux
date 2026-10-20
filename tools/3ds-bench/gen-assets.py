#!/usr/bin/env python3
"""Generate the benchmark's textures procedurally (deterministic, stdlib only).

Writes PNGs into gfx/ (textures + sprite atlas inputs) and icon.png.
tex3ds converts them at build time using the formats in gfx/*.t3s.
"""
import math
import os
import struct
import sys
import zlib

OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "gfx")
ICON = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "icon.png")


def write_png(path, w, h, px):
    """px: list of (r,g,b,a) tuples, row-major."""
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        for x in range(w):
            r, g, b, a = px[y * w + x]
            raw += bytes((clamp(r), clamp(g), clamp(b), clamp(a)))

    def chunk(t, d):
        c = struct.pack(">I", len(d)) + t + d
        return c + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)

    data = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
    data += chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b"")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    if os.path.exists(path) and open(path, "rb").read() == data:
        return  # keep mtime so make does not rebuild
    with open(path, "wb") as f:
        f.write(data)


def clamp(v):
    v = int(round(v))
    return 0 if v < 0 else 255 if v > 255 else v


def hash2(x, y, s=0):
    h = (x * 374761393 + y * 668265263 + s * 2246822519) & 0xFFFFFFFF
    h = ((h ^ (h >> 13)) * 1274126177) & 0xFFFFFFFF
    return ((h ^ (h >> 16)) & 0xFFFF) / 65535.0


def vnoise(x, y, period, s=0):
    """Tileable value noise."""
    xi, yi = int(math.floor(x)), int(math.floor(y))
    fx, fy = x - xi, y - yi
    fx, fy = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy)
    def g(a, b):
        return hash2(a % period, b % period, s)
    a = g(xi, yi) + (g(xi + 1, yi) - g(xi, yi)) * fx
    b = g(xi, yi + 1) + (g(xi + 1, yi + 1) - g(xi, yi + 1)) * fx
    return a + (b - a) * fy


def fbm(x, y, period, octaves=4, s=0):
    v, amp, tot = 0.0, 1.0, 0.0
    for o in range(octaves):
        v += vnoise(x * (1 << o), y * (1 << o), period * (1 << o), s + o) * amp
        tot += amp
        amp *= 0.5
    return v / tot


def img(w, h, f):
    return [f(x, y) for y in range(h) for x in range(w)]


def terrain(x, y):
    n = fbm(x / 16.0, y / 16.0, 8, 5, 1)
    m = fbm(x / 32.0, y / 32.0, 4, 3, 7)
    if m > 0.55:  # rock
        c = 90 + n * 120
        return (c, c * 0.95, c * 0.85, 255)
    return (50 + n * 60, 100 + n * 110, 30 + n * 40, 255)


def leaf(x, y):
    cx, w = 32, 64
    # trunk
    if 28 <= x <= 35 and y >= 48:
        return (90, 60, 30, 255)
    # canopy: stacked triangles
    for top, bot, half in ((2, 26, 18), (14, 40, 24), (26, 52, 30)):
        if top <= y <= bot:
            hw = half * (y - top) / (bot - top)
            if abs(x - cx + 0.5) <= hw:
                n = hash2(x, y, 3)
                return (20 + n * 40, 90 + n * 90, 25 + n * 30, 255)
    return (30, 60, 20, 0)


def metal(x, y):
    n = fbm(x / 64.0, y / 2.0, 2, 3, 11)
    c = 140 + n * 80
    rx, ry = x % 32, y % 32
    if (rx - 4) ** 2 + (ry - 4) ** 2 < 6:
        c = 230
    if x % 32 == 0 or y % 32 == 0:
        c = 70
    return (c * 0.9, c * 0.95, c, 200 + 55 * n)


def brick(x, y):
    row = y // 8
    off = 8 if row % 2 else 0
    if y % 8 == 7 or (x + off) % 16 == 15:
        return (180, 175, 160, 255)
    n = hash2((x + off) // 16, row, 5) * 0.4 + hash2(x, y, 6) * 0.15
    return (150 + 80 * n, 60 + 40 * n, 40 + 20 * n, 255)


def checker(x, y):
    c = 230 if ((x // 8) + (y // 8)) % 2 else 60
    return (c, c, c, 255)


def glow(x, y):
    d = math.hypot(x - 15.5, y - 15.5) / 16.0
    a = max(0.0, 1.0 - d) ** 2
    return (255, 255, 255, 255 * a)


def noise(x, y):
    r = fbm(x / 16.0, y / 16.0, 8, 4, 21)
    g = fbm(x / 16.0, y / 16.0, 8, 4, 22)
    b = fbm(x / 16.0, y / 16.0, 8, 4, 23)
    return (r * 255, g * 255, b * 255, 128 + 127 * fbm(x / 32.0, y / 32.0, 4, 3, 24))


def water(x, y):
    v = 0.5 + 0.25 * math.sin(x / 64.0 * 2 * math.pi * 3) + 0.25 * math.sin((x + y) / 64.0 * 2 * math.pi * 2)
    return (30 + 40 * v, 80 + 60 * v, 160 + 80 * v, 255 if v > 0.25 else 0)


def sprite(i):
    hue = i / 16.0
    col = [255 * (0.5 + 0.5 * math.cos(2 * math.pi * (hue + k / 3.0))) for k in range(3)]
    kind = i % 4

    def f(x, y):
        dx, dy = x - 15.5, y - 15.5
        r = math.hypot(dx, dy)
        a = math.atan2(dy, dx)
        if kind == 0:   # ball
            inside = r < 14
            shade = 1.0 - r / 20.0
        elif kind == 1:  # star
            inside = r < 8 + 6 * abs(math.cos(a * 2.5))
            shade = 1.0
        elif kind == 2:  # diamond
            inside = abs(dx) + abs(dy) < 15
            shade = 0.7 + 0.3 * (dx < 0)
        else:            # ship
            inside = dy > -14 and abs(dx) < (dy + 14) * 0.5 and dy < 12
            shade = 0.8 + 0.2 * (abs(dx) < 3)
        if not inside:
            return (0, 0, 0, 0)
        return (col[0] * shade, col[1] * shade, col[2] * shade, 255)
    return f


def icon(x, y):
    # yellow banana crescent on dark blue
    d1 = math.hypot(x - 20, y - 8)
    d2 = math.hypot(x - 28, y - 2)
    if d1 < 34 and d2 > 34 and 6 < y < 44:
        return (250, 215, 40, 255)
    return (20, 30, 70, 255)


def main():
    write_png(f"{OUT}/terrain.png", 128, 128, img(128, 128, terrain))
    write_png(f"{OUT}/leaf.png", 64, 64, img(64, 64, leaf))
    write_png(f"{OUT}/metal.png", 128, 128, img(128, 128, metal))
    write_png(f"{OUT}/brick.png", 64, 64, img(64, 64, brick))
    write_png(f"{OUT}/checker.png", 64, 64, img(64, 64, checker))
    write_png(f"{OUT}/glow.png", 32, 32, img(32, 32, glow))
    write_png(f"{OUT}/noise.png", 128, 128, img(128, 128, noise))
    write_png(f"{OUT}/water.png", 64, 64, img(64, 64, water))
    for i in range(16):
        write_png(f"{OUT}/sprites/s{i:02d}.png", 32, 32, img(32, 32, sprite(i)))
    write_png(ICON, 48, 48, img(48, 48, icon))


if __name__ == "__main__":
    main()

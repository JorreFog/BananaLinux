#!/usr/bin/env python3
"""gbastress build: generates the data (font, instruments, tables, LZ77 tiles), compiles with clang + ld.lld, fixes
the header check byte, writes out/gbastress-<variant>.gba and its .phases.tsv. One command: python3 build.py
(variants: realistic = L5, L1, L10). Own code, GPL-3.0."""
import math, os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC, OUT, GEN = os.path.join(HERE, "src"), os.path.join(HERE, "out"), os.path.join(HERE, "out", "gen")
RATE = 16777216 / 924          # the mixer's sample rate (timer 0 reload 65536 - 924)
PHASE_FRAMES, NSCENES = 1800, 6
SCENES = ["racer", "shooter", "town", "raster", "loader", "tactics"]
FPS = 16777216 / 280896        # 59.7275

# a 5x7 font of our own for ASCII 32..95 (rows top to bottom, '#' = on)
FONT = {
 ' ': [], '!': ["..#..", "..#..", "..#..", "..#..", "..#..", ".....", "..#.."],
 ',': [".....", ".....", ".....", ".....", ".##..", "..#..", ".#..."],
 '.': [".....", ".....", ".....", ".....", ".....", ".##..", ".##.."],
 '%': ["##...", "##..#", "...#.", "..#..", ".#...", "#..##", "...##"],
 '-': [".....", ".....", ".....", "#####", ".....", ".....", "....."],
 ':': [".....", ".##..", ".##..", ".....", ".##..", ".##..", "....."],
 '?': [".###.", "#...#", "....#", "...#.", "..#..", ".....", "..#.."],
 '0': [".###.", "#...#", "#..##", "#.#.#", "##..#", "#...#", ".###."],
 '1': ["..#..", ".##..", "..#..", "..#..", "..#..", "..#..", ".###."],
 '2': [".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####"],
 '3': ["#####", "...#.", "..#..", "...#.", "....#", "#...#", ".###."],
 '4': ["...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#."],
 '5': ["#####", "#....", "####.", "....#", "....#", "#...#", ".###."],
 '6': ["..##.", ".#...", "#....", "####.", "#...#", "#...#", ".###."],
 '7': ["#####", "....#", "...#.", "..#..", ".#...", ".#...", ".#..."],
 '8': [".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###."],
 '9': [".###.", "#...#", "#...#", ".####", "....#", "...#.", ".##.."],
 'A': [".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"],
 'B': ["####.", "#...#", "#...#", "####.", "#...#", "#...#", "####."],
 'C': [".###.", "#...#", "#....", "#....", "#....", "#...#", ".###."],
 'D': ["###..", "#..#.", "#...#", "#...#", "#...#", "#..#.", "###.."],
 'E': ["#####", "#....", "#....", "####.", "#....", "#....", "#####"],
 'F': ["#####", "#....", "#....", "####.", "#....", "#....", "#...."],
 'G': [".###.", "#...#", "#....", "#.###", "#...#", "#...#", ".####"],
 'H': ["#...#", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"],
 'I': [".###.", "..#..", "..#..", "..#..", "..#..", "..#..", ".###."],
 'J': ["..###", "...#.", "...#.", "...#.", "...#.", "#..#.", ".##.."],
 'K': ["#...#", "#..#.", "#.#..", "##...", "#.#..", "#..#.", "#...#"],
 'L': ["#....", "#....", "#....", "#....", "#....", "#....", "#####"],
 'M': ["#...#", "##.##", "#.#.#", "#.#.#", "#...#", "#...#", "#...#"],
 'N': ["#...#", "#...#", "##..#", "#.#.#", "#..##", "#...#", "#...#"],
 'O': [".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."],
 'P': ["####.", "#...#", "#...#", "####.", "#....", "#....", "#...."],
 'Q': [".###.", "#...#", "#...#", "#...#", "#.#.#", "#..#.", ".##.#"],
 'R': ["####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#"],
 'S': [".####", "#....", "#....", ".###.", "....#", "....#", "####."],
 'T': ["#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.."],
 'U': ["#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."],
 'V': ["#...#", "#...#", "#...#", "#...#", "#...#", ".#.#.", "..#.."],
 'W': ["#...#", "#...#", "#...#", "#.#.#", "#.#.#", "#.#.#", ".#.#."],
 'X': ["#...#", "#...#", ".#.#.", "..#..", ".#.#.", "#...#", "#...#"],
 'Y': ["#...#", "#...#", ".#.#.", "..#..", "..#..", "..#..", "..#.."],
 'Z': ["#####", "....#", "...#.", "..#..", ".#...", "#....", "#####"],
}


def font_data():
    one, tiles, widths = [], [], []
    for code in range(32, 96):
        rows = FONT.get(chr(code), [".....", "#...#", ".#.#.", "..#..", ".#.#.", "#...#", "....."] if chr(code) != ' ' else [])
        bits = [0] * 8
        for y, r in enumerate(rows):
            bits[y] = sum(0x80 >> x for x, ch in enumerate(r) if ch == '#')
        one += bits
        used = max([max([x for x, ch in enumerate(r) if ch == '#'], default=-1) for r in rows] or [-1])
        widths.append(used + 2 if used >= 0 else 4)
        for y in range(8):
            word = 0
            for x in range(8):
                if bits[y] & (0x80 >> x):
                    word |= 1 << (x * 4)
            tiles.append(word)
    return one, tiles, widths


def instruments():
    """four of our own instruments, 8-bit signed: bass (sine), lead (square), pad (saw), drum (noise, decaying)"""
    bass = [round(100 * math.sin(2 * math.pi * i / 64)) for i in range(64)] * 4
    lead = ([90] * 16 + [-90] * 16) * 8
    pad = [round(-110 + 220 * (i % 32) / 31) for i in range(256)]
    seed, drum = 12345, []
    for i in range(2048):
        seed = (seed * 1103515245 + 12345) & 0x7fffffff
        drum.append(round(((seed >> 16) % 200 - 100) * (1 - i / 2048) ** 2))
    insts = [(bass, 64, 64), (lead, 32, 32), (pad, 32, 32), (drum, 0, 0)]   # data, loop length, period
    steps = []
    for k, (data, loop, period) in enumerate(insts):
        row = []
        for n in range(24):
            if period:
                f = (55 if k == 0 else 220) * 2 ** (n / 12)
                row.append(round(f * period / RATE * 4096))
            else:
                row.append(round(4096 * (0.6 + n / 30)))
        steps.append(row)
    return insts, steps


def lz77_vram(data):
    """the BIOS's LZ77 format (type 0x10), with displacement >= 2 so LZ77UnCompVram (16-bit writes) can use it"""
    out = bytearray([0x10, len(data) & 255, (len(data) >> 8) & 255, (len(data) >> 16) & 255])
    i = 0
    while i < len(data):
        flag_at = len(out)
        out.append(0)
        flags = 0
        for b in range(8):
            if i >= len(data):
                break
            best_len, best_d = 0, 0
            for d in range(2, min(i, 4096) + 1):
                l = 0
                while l < 18 and i + l < len(data) and data[i + l] == data[i + l - d]:
                    l += 1
                if l > best_len:
                    best_len, best_d = l, d
                    if l == 18:
                        break
            if best_len >= 3:
                flags |= 0x80 >> b
                v = ((best_len - 3) << 12) | (best_d - 1)
                out += bytes([v >> 8, v & 255])
                i += best_len
            else:
                out.append(data[i])
                i += 1
        out[flag_at] = flags
    while len(out) % 4:
        out.append(0)
    return bytes(out)


def lz_tiles():
    """16 KB of 4bpp tiles with game-like repetition (patterned blocks, some noise)"""
    data, seed = bytearray(), 99
    for t in range(512):
        kind = t % 7
        for y in range(8):
            for x in range(0, 8, 2):
                if kind < 4:
                    a, b = (x + y + kind) & 15, (x + 1 + y + kind) & 15
                else:
                    seed = (seed * 1103515245 + 12345) & 0x7fffffff
                    a = b = (seed >> 20) & 15 if (seed >> 8) % 4 == 0 else kind
                data.append(a | (b << 4))
    return bytes(data)


def carr(name, ctype, vals, per=16):
    lines = [", ".join(str(v) for v in vals[i:i + per]) for i in range(0, len(vals), per)]
    return f"const {ctype} {name}[{len(vals)}] __attribute__((aligned(4))) = {{\n\t" + ",\n\t".join(lines) + "\n};\n"


def gen():
    os.makedirs(GEN, exist_ok=True)
    one, tiles, widths = font_data()
    insts, steps = instruments()
    lz = lz77_vram(lz_tiles())
    sin = [round(4096 * math.sin(2 * math.pi * i / 256)) for i in range(256)]
    c = ['#include "../../src/data.h"\n', carr("font_1bpp", "u8", one), carr("font_tiles", "u32", [f"0x{w:08x}u" for w in tiles], 8),
         carr("font_width", "u8", widths), carr("sin_q12", "s16", sin), carr("lz_tiles", "u8", list(lz), 32)]
    for k, (data, loop, _) in enumerate(insts):
        c.append(carr(f"inst{k}", "s8", data, 32))
    c.append("const s8* const inst_data[4] = { inst0, inst1, inst2, inst3 };\n")
    c.append("const u32 inst_len[4] = { %s };\n" % ", ".join(str(len(d)) for d, _, _ in insts))
    c.append("const u32 inst_loop[4] = { %s };\n" % ", ".join(str(l) for _, l, _ in insts))
    c.append("const u32 note_steps[4][24] = {\n" + ",\n".join("\t{ " + ", ".join(map(str, r)) + " }" for r in steps) + "\n};\n")
    open(os.path.join(GEN, "data.c"), "w").write("".join(c))
    recip = [min(65535, round(65536 * 8 / (y + 16))) for y in range(160)]
    open(os.path.join(GEN, "recip.inc"), "w").write(", ".join(map(str, recip)) + "\n")
    return len(lz)


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        sys.stderr.write(" ".join(cmd) + "\n" + r.stderr[-4000:])
        sys.exit(1)
    if r.stderr.strip():
        sys.stderr.write(r.stderr[-2000:])


def build(variant, level, phase_frames=PHASE_FRAMES):
    obj = os.path.join(OUT, "obj-" + variant)
    os.makedirs(obj, exist_ok=True)
    cflags = ["clang", "--target=armv4t-none-eabi", "-mcpu=arm7tdmi", "-mthumb", "-O2", "-ffreestanding",
              "-fno-strict-aliasing", "-fno-exceptions", "-Wall", "-Wextra", "-Wno-unused-parameter",
              f"-DLEVEL={level}", f"-DPHASE_FRAMES={phase_frames}", "-I" + SRC, "-I" + GEN]
    objs = []
    for src in [os.path.join(SRC, "main.c"), os.path.join(SRC, "fast.c"), os.path.join(GEN, "data.c")]:
        o = os.path.join(obj, os.path.basename(src) + ".o")
        run(cflags + ["-c", src, "-o", o])
        objs.append(o)
    o = os.path.join(obj, "crt0.o")
    run(["clang", "--target=armv4t-none-eabi", "-mcpu=arm7tdmi", "-c", os.path.join(SRC, "crt0.s"), "-o", o])
    elf = os.path.join(obj, "gbastress.elf")
    run(["ld.lld", "-T", os.path.join(SRC, "gba.ld"), "--no-undefined", o] + objs + ["-o", elf])
    rom_path = os.path.join(OUT, f"gbastress-{variant}.gba")
    run(["llvm-objcopy", "-O", "binary", elf, rom_path])
    rom = bytearray(open(rom_path, "rb").read())
    rom[0xBD] = (-(sum(rom[0xA0:0xBD]) + 0x19)) & 0xFF
    while len(rom) % 1024:
        rom.append(0)
    open(rom_path, "wb").write(rom)
    with open(rom_path[:-4] + ".phases.tsv", "w") as f:
        for p in range(40):                        # 20 min of phases (soak runs loop the same scenes)
            f.write(f"{p * phase_frames / FPS:.1f}\t{SCENES[p % NSCENES]}\n")
    return rom_path, len(rom)


def main():
    lz = gen()
    # quick: the realistic load in 10 s phases (a 50 s loop) for A/B series; its checksums differ from realistic's
    for variant, level, pf in (("realistic", 5, PHASE_FRAMES), ("L1", 1, PHASE_FRAMES), ("L10", 10, PHASE_FRAMES), ("quick", 5, 600)):
        path, size = build(variant, level, pf)
        print(f"{path}: {size} bytes (LZ77 tiles {lz} bytes)")


if __name__ == "__main__":
    main()

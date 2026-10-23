#!/usr/bin/env python3
"""Compare two directories of PNG frame dumps (panda3ds-bench --dump), file by file.

    imgdiff.py BASE_DIR NEW_DIR [--min-psnr 30] [--max-diff-pct 2.0] [--tolerance 8] [--diff-dir DIR]

For each PNG present in either directory it prints the PSNR (RGB, dB; "inf" when identical), the percentage of
pixels whose largest channel difference is above --tolerance, the largest channel difference, and the same for the
top screen (rows 0-239) and the bottom screen (rows 240-479) of a 400x480 dump. With --diff-dir it writes, per file
that differs, a PNG of the absolute difference amplified 4x.

Exit status: 0 if every file is in both directories with the same size, PSNR >= --min-psnr and differing
pixels <= --max-diff-pct; 1 otherwise; 2 for usage errors. Python 3 standard library only.
"""
import argparse
import math
import os
import struct
import sys
import zlib


def read_png(path):
    """Returns (width, height, rows) with rows as bytearrays of RGB bytes. 8-bit RGB/RGBA/grey, non-interlaced."""
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path}: not a PNG")
    pos, idat, w = 8, bytearray(), None
    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if kind == b"IHDR":
            w, h, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", body)
            if depth != 8 or interlace or ctype not in (0, 2, 4, 6):
                raise ValueError(f"{path}: unsupported PNG (depth {depth}, type {ctype}, interlace {interlace})")
        elif kind == b"IDAT":
            idat += body
        elif kind == b"IEND":
            break
    if w is None:
        raise ValueError(f"{path}: no IHDR")
    bpp = {0: 1, 2: 3, 4: 2, 6: 4}[ctype]
    raw = zlib.decompress(bytes(idat))
    stride = w * bpp
    rows, prev = [], bytearray(stride)
    for y in range(h):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        if f == 1:
            for i in range(bpp, stride):
                line[i] = (line[i] + line[i - bpp]) & 0xFF
        elif f == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif f == 3:
            for i in range(stride):
                left = line[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + ((left + prev[i]) >> 1)) & 0xFF
        elif f == 4:
            for i in range(stride):
                a = line[i - bpp] if i >= bpp else 0
                b = prev[i]
                c = prev[i - bpp] if i >= bpp else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pred = a if pa <= pb and pa <= pc else b if pb <= pc else c
                line[i] = (line[i] + pred) & 0xFF
        elif f != 0:
            raise ValueError(f"{path}: bad filter {f}")
        prev = line
        if ctype == 2:
            rows.append(line)
        elif ctype == 6:
            rows.append(bytearray(b for i, b in enumerate(line) if i % 4 != 3))
        elif ctype == 0:
            rows.append(bytearray(v for v in line for _ in range(3)))
        else:  # grey + alpha
            rows.append(bytearray(line[i] for i in range(0, stride, 2) for _ in range(3)))
    return w, h, rows


def write_png(path, w, h, rows):
    raw = b"".join(b"\x00" + bytes(r) for r in rows)

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
                chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


class Stats:
    def __init__(self):
        self.sq = 0
        self.n = 0
        self.diff_px = 0
        self.px = 0
        self.max = 0

    def psnr(self):
        if self.sq == 0:
            return math.inf
        return 10 * math.log10(255.0 ** 2 / (self.sq / self.n))

    def pct(self):
        return 100.0 * self.diff_px / self.px if self.px else 0.0

    def fmt(self):
        p = self.psnr()
        ps = "inf" if p == math.inf else f"{p:.2f}"
        return f"psnr {ps:>6} dB  diff {self.pct():6.3f}%  max {self.max:3d}"


def compare(a_path, b_path, tol, diff_path):
    wa, ha, ra = read_png(a_path)
    wb, hb, rb = read_png(b_path)
    if (wa, ha) != (wb, hb):
        return None, f"size {wa}x{ha} vs {wb}x{hb}"
    total, top, bot = Stats(), Stats(), Stats()
    out = [] if diff_path else None
    for y in range(ha):
        la, lb = ra[y], rb[y]
        part = top if y < 240 else bot
        drow = bytearray(len(la)) if out is not None else None
        sq = 0
        for x in range(0, len(la), 3):
            d0 = abs(la[x] - lb[x])
            d1 = abs(la[x + 1] - lb[x + 1])
            d2 = abs(la[x + 2] - lb[x + 2])
            m = max(d0, d1, d2)
            if m:
                sq += d0 * d0 + d1 * d1 + d2 * d2
                if m > tol:
                    part.diff_px += 1
                if m > part.max:
                    part.max = m
                if drow is not None:
                    drow[x], drow[x + 1], drow[x + 2] = min(255, d0 * 4), min(255, d1 * 4), min(255, d2 * 4)
        part.sq += sq
        part.n += len(la)
        part.px += len(la) // 3
        if out is not None:
            out.append(drow)
    for p in (top, bot):
        total.sq += p.sq
        total.n += p.n
        total.px += p.px
        total.diff_px += p.diff_px
        total.max = max(total.max, p.max)
    if out is not None and total.sq:
        write_png(diff_path, wa, ha, out)
    return (total, top, bot), None


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("base")
    ap.add_argument("new")
    ap.add_argument("--min-psnr", type=float, default=30.0, help="fail below this PSNR in dB (default 30)")
    ap.add_argument("--max-diff-pct", type=float, default=2.0, help="fail above this %% of differing pixels (default 2)")
    ap.add_argument("--tolerance", type=int, default=8, help="a pixel differs if a channel differs by more (default 8)")
    ap.add_argument("--diff-dir", help="write amplified difference images here")
    args = ap.parse_args()
    for d in (args.base, args.new):
        if not os.path.isdir(d):
            print(f"not a directory: {d}", file=sys.stderr)
            return 2
    if args.diff_dir:
        os.makedirs(args.diff_dir, exist_ok=True)

    names = sorted(set(f for d in (args.base, args.new) for f in os.listdir(d) if f.lower().endswith(".png")))
    if not names:
        print("no PNGs", file=sys.stderr)
        return 1
    bad = 0
    for name in names:
        a, b = os.path.join(args.base, name), os.path.join(args.new, name)
        if not (os.path.exists(a) and os.path.exists(b)):
            print(f"{name:16s} MISSING in {'base' if not os.path.exists(a) else 'new'}")
            bad += 1
            continue
        res, err = compare(a, b, args.tolerance, os.path.join(args.diff_dir, name) if args.diff_dir else None)
        if err:
            print(f"{name:16s} FAIL {err}")
            bad += 1
            continue
        total, top, bot = res
        ok = total.psnr() >= args.min_psnr and total.pct() <= args.max_diff_pct
        bad += not ok
        print(f"{name:16s} {'ok  ' if ok else 'FAIL'} {total.fmt()}   top {top.fmt()}   bottom {bot.fmt()}")
    print(f"{len(names) - bad}/{len(names)} match (min psnr {args.min_psnr} dB, max diff {args.max_diff_pct}% "
          f"at tolerance {args.tolerance})")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())

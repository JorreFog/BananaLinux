#!/usr/bin/env python3
"""results.py: the ROM's results table (0x0203FF00) from lrhost's `--peek 0x0203FF00 224` output (stdin) or a .srm
file (argument; the same records at offset 16): per phase frames, checksum, GBA CPU use, overruns."""
import struct, sys
SC = ["racer", "shooter", "town", "raster", "loader", "tactics"]
if len(sys.argv) > 1:
    raw = open(sys.argv[1], "rb").read()
    recs, done = raw[16:16 + 12 * 16], 12
else:
    txt = sys.stdin.read().split("peek 0203ff00", 1)[1].split("\n", 1)[1]
    raw = bytes.fromhex("".join(txt.split()))
    done = struct.unpack_from("<I", raw, 12)[0]
    recs = raw[16:]
for i in range(min(done, 12)):
    f, c, a, m, o, _ = struct.unpack_from("<IIHHHH", recs, i * 16)
    if f == 0 or f == 0xFFFFFFFF:
        break
    print(f"{i:2d} {SC[i % 6]:8s} frames {f} checksum {c:08x} cpu avg {a}% max {m}% overruns {o}")

#!/usr/bin/env python3
# bltargets.py <jit.code> <perf map>: BL targets from the blocks (mj9_/mj7_) into the stubs, by stub name + offset
import struct, sys, collections
data = open(sys.argv[1], 'rb').read(); stubs = []; names = {}
for l in open(sys.argv[2]):
    a, s, n = l.split()[:3]; a = int(a, 16); names[a] = n
    if n.startswith('stub_'): stubs.append((a, int(s, 16), n))
c = collections.Counter(); i = 0
while i + 12 <= len(data):
    at, n = struct.unpack_from('<QI', data, i); i += 12
    if names.get(at, '?').startswith(('mj9_', 'mj7_')):
        for k in range(0, n - 3, 4):
            w = struct.unpack_from('<I', data, i + k)[0]
            if (w >> 26) == 0x25:   # BL imm26
                off = w & 0x3FFFFFF
                if off & 0x2000000: off -= 0x4000000
                t = at + k + off * 4
                for a, s, nm in stubs:
                    if a <= t < a + s: c[(nm, t - a)] += 1; break
                else: c[('other', 0)] += 1
    i += n
for (nm, o), v in sorted(c.items()): print(f'{nm} +0x{o:x}: {v}')

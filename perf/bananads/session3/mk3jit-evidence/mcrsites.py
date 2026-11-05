#!/usr/bin/env python3
# mcrsites.py <qc dir> <hex target>: blocks calling <target> (e.g. A_MCR) with the guest instruction word stored
# before the call (MOVI2R W0 + STR W0, [x29, CurInstr]), weighted by the call's executions per frame (1650-2150)
import sys, struct, subprocess, tempfile, os, re, collections
D = sys.argv[1]; T = int(sys.argv[2], 16)
data = open(D + '/f2150.code', 'rb').read(); recs = {}
i = 0
while i + 12 <= len(data):
    at, n = struct.unpack_from('<QI', data, i); i += 12; recs[at] = data[i:i + n]; i += n
cnt = collections.Counter()
for f, sg in (('f1650.tsv', -1), ('f2150.tsv', 1)):
    for l in open(D + '/' + f):
        v, kk, key, n, m, b = l.rstrip('\n').split('\t')
        if v == '0' and kk == 'addr': cnt[int(key, 16)] += sg * int(n)
OD = '/home/user/perf/toolchain/aarch64--glibc--stable-2025.08-1/bin/aarch64-linux-objdump'
blob = b''.join(recs.values())
res = collections.Counter()
for at, code in recs.items():
    # quick filter: a BL to T from somewhere in this record
    hit = False
    for j in range(0, len(code) - 3, 4):
        w = struct.unpack_from('<I', code, j)[0]
        if (w >> 26) == 0x25:
            off = (w & 0x3ffffff); off = off - (1 << 26) if off & (1 << 25) else off
            if at + j + off * 4 == T: hit = True; break
    if not hit: continue
    with tempfile.NamedTemporaryFile(delete=False) as f: f.write(code); fn = f.name
    out = subprocess.run([OD, '-D', '-b', 'binary', '-m', 'aarch64', '--no-show-raw-insn', '--adjust-vma=0x%x' % at, fn], capture_output=True, text=True).stdout
    os.unlink(fn)
    lines = [(int(m.group(1), 16), m.group(2)) for m in (re.match(r'^\s+([0-9a-f]+):\t(.*)', l) for l in out.splitlines()) if m]
    tbstart = None; tbcount = 0
    for k, (a, t) in enumerate(lines):
        if a in cnt: tbstart = a
        if t.startswith('bl\t') and int(t.split()[-1], 16) == T:
            # the instruction word: the last mov/movk pair into w0 before it
            word = None; seg = lines[max(0, k - 40):k]
            idx = max((q for q, (a2, t2) in enumerate(seg) if t2.startswith('str\tw0, [x29, #184]')), default=None)
            if idx is not None:
                for a2, t2 in reversed(seg[max(0, idx - 3):idx]):
                    m = re.match(r'(mov|movk)\s+w0, #(0x[0-9a-f]+|\d+)(, lsl #16)?', t2)
                    if m:
                        v = int(m.group(2), 0) << (16 if m.group(3) else 0)
                        word = v if word is None else word | v
                        if m.group(1) == 'mov': break
            # executions: the TB holding the call (from its start), its count / its length
            ts = max(a2 for a2, _ in lines[:k + 1] if a2 in cnt) if any(a2 in cnt for a2, _ in lines[:k + 1]) else None
            if ts is None: continue
            L = 0
            for a2, t2 in lines:
                if a2 < ts: continue
                L += 1
                if re.match(r'^(b|bl|br|blr|ret|cbz|cbnz|tbz|tbnz|b\.\w+)\b', t2): break
            res[(word, '%x' % at)] += cnt[ts] / L / 500
tot = sum(res.values()); print('calls/frame: %.0f' % tot)
byw = collections.Counter()
for (w, at), v in res.items(): byw[w] += v
for w, v in byw.most_common(20):
    if w is None: print('%7.0f  ?' % v); continue
    print('%7.0f  %08x  cp%d c%d,c%d,%d  id %03x' % (v, w, (w >> 8) & 15, (w >> 16) & 15, w & 15, (w >> 5) & 7, (((w >> 16) & 15) << 8) | ((w & 15) << 4) | ((w >> 5) & 7)))

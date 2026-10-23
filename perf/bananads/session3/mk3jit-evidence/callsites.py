#!/usr/bin/env python3
# callsites.py <qc dir> <hex target> [n]: the hottest BL sites to <target> (executions per frame, 1650-2150), with the
# block name and the 8 instructions before each
import sys, struct, subprocess, tempfile, os, re, collections
D = sys.argv[1]; T = int(sys.argv[2], 16); N = int(sys.argv[3]) if len(sys.argv) > 3 else 5
data = open(D + '/f2150.code', 'rb').read(); recs = {}
i = 0
while i + 12 <= len(data):
    at, n = struct.unpack_from('<QI', data, i); i += 12; recs[at] = data[i:i + n]; i += n
names = {}
for l in open(D + '/f2150.map'):
    a, s, n = l.split()[:3]; names[int(a, 16)] = n
cnt = collections.Counter()
for f, sg in (('f1650.tsv', -1), ('f2150.tsv', 1)):
    for l in open(D + '/' + f):
        v, kk, key, n, m, b = l.rstrip('\n').split('\t')
        if v == '0' and kk == 'addr': cnt[int(key, 16)] += sg * int(n)
OD = '/home/user/perf/toolchain/aarch64--glibc--stable-2025.08-1/bin/aarch64-linux-objdump'
BR = re.compile(r'^(b|bl|br|blr|ret|cbz|cbnz|tbz|tbnz|b\.\w+)\b')
sites = []
for at, code in recs.items():
    js = []
    for j in range(0, len(code) - 3, 4):
        w = struct.unpack_from('<I', code, j)[0]
        if (w >> 26) == 0x25:
            off = (w & 0x3ffffff); off = off - (1 << 26) if off & (1 << 25) else off
            if at + j + off * 4 == T: js.append(j)
    if not js: continue
    with tempfile.NamedTemporaryFile(delete=False) as f: f.write(code); fn = f.name
    out = subprocess.run([OD, '-D', '-b', 'binary', '-m', 'aarch64', '--no-show-raw-insn', '--adjust-vma=0x%x' % at, fn], capture_output=True, text=True).stdout
    os.unlink(fn)
    lines = [(int(m.group(1), 16), m.group(2)) for m in (re.match(r'^\s+([0-9a-f]+):\t(.*)', l) for l in out.splitlines()) if m]
    pos = {a: k for k, (a, t) in enumerate(lines)}
    for j in js:
        k = pos[at + j]
        # the TB holding the call: the nearest counted start at or before it with no branch in between
        q = k
        while q > 0 and lines[q][0] not in cnt and not BR.match(lines[q - 1][1]): q -= 1
        if lines[q][0] not in cnt: continue
        L = 0
        for a2, t2 in lines[q:]:
            L += 1
            if BR.match(t2): break
        sites.append((cnt[lines[q][0]] / L / 500, names.get(at, '%x' % at), lines[max(0, k - 8):k + 1]))
sites.sort(key=lambda s: -s[0])
print('sites: %d, calls/frame %.0f' % (len(sites), sum(s[0] for s in sites)))
for c, nm, ls in sites[:N]:
    print('%.0f/frame  %s' % (c, nm))
    for a, t in ls: print('    %x: %s' % (a, t))

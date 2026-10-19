#!/usr/bin/env python3
"""jitblocks.py <qc dir> [frames=500] [top=40]: the emulator thread's (vcpu 0) JIT-code instructions per frame over
1650-2150, by block (perf map name), and the dynamic instruction mix of the JIT code (each qemu TB disassembled from
the code dump up to its first branch; executions = its count / its length), classified as census.py does."""
import sys, struct, bisect, collections, subprocess, re, os, tempfile
D = sys.argv[1]; FR = int(sys.argv[2]) if len(sys.argv) > 2 else 500; TOP = int(sys.argv[3]) if len(sys.argv) > 3 else 40
def load(t):
    c = collections.Counter()
    for l in open(t):
        v, k, key, n, m, b = l.rstrip('\n').split('\t')
        if v == '0' and k == 'addr': c[int(key, 16)] += int(n)
    return c
A = load(D + '/f1650.tsv'); B = load(D + '/f2150.tsv'); C = collections.Counter(B); C.subtract(A)
# code dump: the last record per address wins (as the map)
data = open(D + '/f2150.code', 'rb').read(); recs = {}
i = 0
while i + 12 <= len(data):
    at, n = struct.unpack_from('<QI', data, i); i += 12; recs[at] = data[i:i + n]; i += n
names = {}
for l in open(D + '/f2150.map'):
    a, s, n = l.split()[:3]; names[int(a, 16)] = (int(s, 16), n)
starts = sorted(names)
lo = starts[0] - 0x100000; hi = starts[-1] + 0x100000
def blk(a):
    j = bisect.bisect_right(starts, a) - 1
    if j >= 0 and a < starts[j] + names[starts[j]][0]: return starts[j]
    return None
perblk = collections.Counter(); tbs = collections.defaultdict(list); unk = 0
for a, n in C.items():
    if not (lo <= a < hi) or n <= 0: continue
    s = blk(a)
    if s is None: unk += n; continue
    perblk[s] += n; tbs[s].append((a, n))
tot = sum(perblk.values()) + unk
print(f"JIT code (vcpu 0): {tot/FR/1e3:.1f}k insns/frame; outside any mapped block {unk/FR/1e3:.1f}k; {len(perblk)} blocks ran")
byname = collections.Counter()
for s, n in perblk.items(): byname[names[s][1]] += n
cum = 0
print(f"top {TOP} blocks (k insns/frame, cumulative %, bytes):")
for s, n in sorted(perblk.items(), key=lambda x: -x[1])[:TOP]:
    cum += n
    print(f"  {n/FR/1e3:8.1f}k {100*cum/tot:5.1f}%  {names[s][1]:24s} {names[s][0]:6d}B @{s:x}")
OD = '/home/user/perf/toolchain/aarch64--glibc--stable-2025.08-1/bin/aarch64-linux-objdump'
def dis(buf):
    with tempfile.NamedTemporaryFile(delete=False) as f: f.write(buf); fn = f.name
    out = subprocess.run([OD, '-D', '-b', 'binary', '-m', 'aarch64', '--no-show-raw-insn', fn], capture_output=True, text=True).stdout
    os.unlink(fn)
    return [l.split('\t', 1)[1].strip() for l in out.splitlines() if re.match(r'^\s+[0-9a-f]+:\t', l)]
BR = re.compile(r'^(b|bl|br|blr|ret|cbz|cbnz|tbz|tbnz|b\.\w+|brk|udf)\b')
sys.path.insert(0, '/home/user/perf3/mk3jit-w/tools')
from jitcls import cls
txt = collections.Counter(); bigr = collections.Counter(); mix = collections.Counter(); seq = collections.Counter(); calls = collections.Counter()
for s, lst in tbs.items():
    code = recs.get(s)
    if code is None: continue
    ins = dis(code)
    for a, n in lst:
        k = (a - s) // 4; L = 0; body = []
        while k + L < len(ins):
            t = ins[k + L]; body.append(t); L += 1
            if BR.match(t) or L >= 512: break
        if not L: continue
        ex = n / L
        for t in body:
            mix[cls(t)] += ex
            txt[re.sub(r'0x[0-9a-f]{5,}', 'ADDR', t)] += ex
            if t.split()[0] == 'bl':
                tg = (s + int(t.split()[-1], 16)) & 0xffffffffffff; b2 = blk(tg)
                calls[names[b2][1] + ('+%x' % (tg - b2) if tg != b2 else '') if b2 is not None else '%x' % tg] += ex
print("dynamic mix of the JIT code (k insns/frame; census.py's classes):")
for k, v in mix.most_common(): print(f"  {v/FR/1e3:8.1f}k  {100*v/tot:5.1f}%  {k}")
print("bl calls from blocks (k calls/frame, by target offset in the dump: resolve with the map):")
for k, v in calls.most_common(15): print(f"  {v/FR/1e3:8.2f}k  {k}")
print("top dynamic instruction texts (k/frame):")
for k, v in txt.most_common(60): print(f"  {v/FR/1e3:8.1f}k  {k}")

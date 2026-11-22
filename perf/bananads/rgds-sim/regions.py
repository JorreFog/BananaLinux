#!/usr/bin/env python3
"""regions.py <run dir> [thread name or vcpu] [N1 N2]: one thread's L1D misses per frame by buffer and by function.

Needs a run of the v2 plugin (it writes f<N>.regions: misses per code location and 64 KB region). Buffers are told
apart by the address map (f<N>.maps): every mapping >= 64 KB (the big buffers are mmap'd on their own), named by its
file or as anon@<start>+<size>; a [heap] mapping is split into 64 KB regions. For each buffer: load misses, store
misses (allocating), write-streamed store lines (no allocate, no stall), misses to DRAM, and the functions behind them.
Example: rgds-sim/regions.py /tmp/rgds/L5-2x bananads-2d
"""
import sys, os, json, bisect, collections, signal
signal.signal(signal.SIGPIPE, signal.SIG_DFL)
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import report as R

d = sys.argv[1]
want = sys.argv[2] if len(sys.argv) > 2 else '0'
cmd = open(os.path.join(d, 'cmd')).readline()
n1, n2 = 100, 300
if '--frames' in cmd:
    a = cmd.split('--frames')[1].split()[0]; n1, n2 = int(a.split(':')[0]), int(a.split(':')[1])
if len(sys.argv) > 4: n1, n2 = int(sys.argv[3]), int(sys.argv[4])
frames = n2 - n1


def load(prefix):
    rows = collections.defaultdict(lambda: [0] * 6)
    with open(prefix + '.regions') as f:
        f.readline()
        for l in f:
            v, kind, key, reg, *c = l.rstrip('\n').split('\t')
            r = rows[(int(v), kind, key, int(reg, 16))]
            for i, x in enumerate(c): r[i] += int(x)
    return rows


def threads(prefix):
    out = {}
    with open(prefix + '.threads') as f:
        f.readline()
        for l in f:
            a = l.split('\t'); out[int(a[0])] = a[2]
    return out


A = load(os.path.join(d, 'f%d' % n1)); B = load(os.path.join(d, 'f%d' % n2))
th = threads(os.path.join(d, 'f%d' % n2))
vcpu = None
for v, name in th.items():
    if want == str(v) or want == name: vcpu = v
if vcpu is None: sys.exit('no thread %s (threads: %s)' % (want, th))
maps = []
for l in open(os.path.join(d, 'f%d.maps' % n2)):
    p = l.split()
    s, e = (int(x, 16) for x in p[0].split('-'))
    name = p[5] if len(p) >= 6 else ''
    maps.append((s, e, name))
maps.sort()
starts = [m[0] for m in maps]
blocks = []
mp = os.path.join(d, 'f%d.map' % n2)
if os.path.exists(mp):
    for l in open(mp):
        p = l.split()
        if len(p) >= 3: blocks.append((int(p[0], 16), int(p[1], 16), p[2]))
blocks.sort()


def buf(region):
    i = bisect.bisect_right(starts, region) - 1
    if i < 0 or region >= maps[i][1]: return 'unmapped@%x' % region
    s, e, name = maps[i]
    size = e - s
    if name == '[heap]' or (not name and size >= 64 << 20):   # heaps / arenas: per 64 KB
        return '%s@%x(64K)' % (name or 'arena', region)
    if name.startswith('/'): return os.path.basename(name)
    return '%s@%x+%dK' % (name or 'anon', s, size >> 10)


_dm = {}


def short(sym):
    if sym in _dm: return _dm[sym]
    out = sym
    if sym.startswith('_Z'):
        import subprocess
        try: out = subprocess.run(['c++filt', sym], capture_output=True, text=True, timeout=10).stdout.strip() or sym
        except Exception: pass
        out = out.split('(')[0].replace('melonDS::', '')
    _dm[sym] = out
    return out


def fname(kind, key):
    if kind == 'sym': return key
    a = int(key, 16)
    if blocks and blocks[0][0] - 0x100000 <= a < blocks[-1][0] + blocks[-1][1] + 0x100000: return 'jit-code'
    return 'lib@' + key


per_buf = collections.defaultdict(lambda: [0.0] * 6)
per_buf_fn = collections.defaultdict(lambda: collections.defaultdict(lambda: [0.0] * 6))
for k in set(A) | set(B):
    if k[0] != vcpu: continue
    a = A.get(k, [0] * 6); b = B.get(k, [0] * 6)
    dlt = [(y - x) / frames for x, y in zip(a, b)]
    bn = buf(k[3]); fn = fname(k[1], k[2])
    for i in range(6):
        per_buf[bn][i] += dlt[i]; per_buf_fn[bn][fn][i] += dlt[i]
print('thread %d (%s), frames %d-%d, per frame: L1D load misses, allocating store misses, write-streamed store lines'
      ' (no allocate), and of them to DRAM' % (vcpu, th[vcpu], n1, n2))
tot = [sum(v[i] for v in per_buf.values()) for i in range(6)]
print('total: ld %.1fk st %.1fk ws %.1fk | DRAM: ld %.1fk st %.1fk ws(no L3 alloc) %.1fk' % (
    tot[0] / 1e3, tot[1] / 1e3, tot[4] / 1e3, tot[2] / 1e3, tot[3] / 1e3, tot[5] / 1e3))
# heap regions: grouped by the function that misses most there (one buffer usually spans several 64 KB regions)
grouped = collections.defaultdict(lambda: [0.0] * 6)
gfn = collections.defaultdict(lambda: collections.defaultdict(lambda: [0.0] * 6))
gcount = collections.Counter()
for bn in list(per_buf):
    if '(64K)' not in bn: continue
    fns = per_buf_fn[bn]
    top = max(fns.items(), key=lambda x: x[1][0] + x[1][1] + x[1][4])[0]
    g = 'heap regions led by %s' % short(top)[:30]
    for i in range(6): grouped[g][i] += per_buf[bn][i]
    for f, c in fns.items():
        for i in range(6): gfn[g][f][i] += c[i]
    gcount[g] += 1
    del per_buf[bn]
for g, v in grouped.items():
    per_buf['%s (%dx64K)' % (g, gcount[g])] = v; per_buf_fn['%s (%dx64K)' % (g, gcount[g])] = gfn[g]
print('%-60s %8s %8s %8s %8s %8s  %s' % ('buffer', 'ld miss', 'st miss', 'ws line', 'ld DRAM', 'st DRAM', 'functions (ld+st misses)'))
for bn, v in sorted(per_buf.items(), key=lambda x: -(x[1][0] + x[1][1] + x[1][4])):
    if v[0] + v[1] + v[4] < 0.005 * (tot[0] + tot[1] + tot[4]): continue
    fns = sorted(per_buf_fn[bn].items(), key=lambda x: -(x[1][0] + x[1][1] + x[1][4]))[:4]
    print('%-60s %7.1fk %7.1fk %7.1fk %7.1fk %7.1fk  %s' % (bn[:60], v[0] / 1e3, v[1] / 1e3, v[4] / 1e3, v[2] / 1e3, v[3] / 1e3,
          '; '.join('%s %.1fk' % (short(f)[:48], (c[0] + c[1] + c[4]) / 1e3) for f, c in fns)))

#!/usr/bin/env python3
# l1i-diff.py <runA dir> <runB dir> [vcpu] [counter]: per-symbol counter (default l1i_miss) per frame, A vs B, top movers
import sys, os, collections
sys.path.insert(0, '/home/user/BananaLinux/perf/bananads/rgds-sim')
import report
a, b = sys.argv[1], sys.argv[2]; v = int(sys.argv[3]) if len(sys.argv) > 3 else 0; ctr = sys.argv[4] if len(sys.argv) > 4 else 'l1i_miss'
def pf(d):
    A = report.load_run(os.path.join(d, 'f100')); B = report.load_run(os.path.join(d, 'f300'))
    return report.per_frame(A, B, 200)
pa, pb = pf(a), pf(b)
ia, ib = pa[v]['items'], pb[v]['items']
def tot(items, c): return sum(x.get(c, 0) for x in items.values())
print('%s per frame, vcpu %d: A %.1fk  B %.1fk' % (ctr, v, tot(ia, ctr)/1e3, tot(ib, ctr)/1e3))
keys = set(ia) | set(ib)
rows = []
for k in keys:
    xa = ia.get(k, {}).get(ctr, 0); xb = ib.get(k, {}).get(ctr, 0)
    rows.append((xb - xa, xa, xb, ia.get(k, {}).get('insn', 0), ib.get(k, {}).get('insn', 0), k))
rows.sort(key=lambda r: -abs(r[0]))
print('%10s %10s %10s %10s %10s  %s' % ('delta', 'A', 'B', 'insnA', 'insnB', 'symbol'))
for d, xa, xb, na, nb, k in rows[:40]:
    print('%10.0f %10.0f %10.0f %10.0f %10.0f  [%s] %s' % (d, xa, xb, na, nb, k[0], k[1][:90]))

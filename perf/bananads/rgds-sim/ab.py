#!/usr/bin/env python3
"""ab.py A/report.json B/report.json params.json: per-thread and per-bucket deltas between two rgds-sim runs."""
import sys, json, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import report as R
A = json.load(open(sys.argv[1])); B = json.load(open(sys.argv[2])); p = R.load_params(sys.argv[3])
pf = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'params-fit.json')
pfit = R.load_params(pf) if os.path.exists(pf) else None
print('rgds-sim A/B (model %s)' % p.get('version', '?'))
print('A: ' + open(os.path.join(os.path.dirname(sys.argv[1]), 'cmd')).readline().strip())
print('B: ' + open(os.path.join(os.path.dirname(sys.argv[2]), 'cmd')).readline().strip())
def pct(a, b): return 100.0 * (b - a) / a if a else float('nan')
EV = ['insn', 'base', 'l1i_miss', 'l1i_l3miss', 'ld_miss', 'ld_l3miss', 'st_miss', 'st_l3miss', 'itlb_l2', 'itlb_walk',
      'dtlb_l2', 'dtlb_walk', 'bcond_miss', 'bind_miss']
for v in sorted(set(A) | set(B), key=int):
    a = A.get(v); b = B.get(v)
    if not a or not b:
        print('thread %s only in %s' % (v, 'A' if a else 'B')); continue
    ta, tb = a['totals'], b['totals']
    print('\n== thread %s (%s): ms/frame @1104 %.3f -> %.3f (%+.2f%%)   @1992 %.3f -> %.3f (%+.2f%%)' % (
        v, b['name'], R.ms_at(ta, p, 1104), R.ms_at(tb, p, 1104), pct(R.ms_at(ta, p, 1104), R.ms_at(tb, p, 1104)),
        R.ms_at(ta, p, 1992), R.ms_at(tb, p, 1992), pct(R.ms_at(ta, p, 1992), R.ms_at(tb, p, 1992))))
    if pfit:
        print('  sensitivity (params-fit.json, %s): @1104 %.3f -> %.3f (%+.2f%%)' % (pfit.get('version'), R.ms_at(ta, pfit, 1104),
              R.ms_at(tb, pfit, 1104), pct(R.ms_at(ta, pfit, 1104), R.ms_at(tb, pfit, 1104))))
    print('  ' + '  '.join('%s %.1fk->%.1fk (%+.1f%%)' % (e, ta.get(e, 0) / 1e3, tb.get(e, 0) / 1e3, pct(ta.get(e, 0), tb.get(e, 0)))
                         for e in EV if abs(ta.get(e, 0)) + abs(tb.get(e, 0)) >= 1))
    ba, bb = a['buckets'], b['buckets']
    rows = []
    for k in set(ba) | set(bb):
        ma = R.ms_at(ba.get(k, {}), p, 1104); mb = R.ms_at(bb.get(k, {}), p, 1104)
        rows.append((abs(mb - ma), k, ma, mb, ba.get(k, {}).get('insn', 0), bb.get(k, {}).get('insn', 0)))
    print('  %-16s %9s %9s %9s %10s %10s' % ('bucket', 'ms A', 'ms B', 'delta', 'insns A', 'insns B'))
    for d, k, ma, mb, ia, ib in sorted(rows, reverse=True):
        if d < 0.0005 and abs(ib - ia) < 100: continue
        print('  %-16s %9.3f %9.3f %+9.3f %9.1fk %9.1fk' % (k, ma, mb, mb - ma, ia / 1e3, ib / 1e3))

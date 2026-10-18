#!/usr/bin/env python3
"""calibrate.py: fit rgds-sim's free cost parameters to device frame times.

calibrate.py <device.tsv> <runs-dir> [--combine max|sum|main|mix] [--free base_scale,l3_cyc,dram_ns,mispredict_cyc]
             [--write params.json] [--version NAME]
device.tsv: lines "run<TAB>mhz<TAB>fps" (comments with #); run = a run label under <runs-dir> holding report.json
(rgds-sim/run.sh's output for that workload). A run's frame time is modeled from its threads: thread 0 (the
emulator's) and, if present, the busiest other thread (a renderer): main = thread 0 only, max = max of the two,
sum = both serialized, mix = max + alpha*min (alpha fitted).
The fit minimises the squared relative error of the frame time (Nelder-Mead on log-parameters; numpy only).
It prints every row's error, the RMS and max error, and two out-of-sample checks: leave one ROM out (fit on the
rest, predict it) and fit on 1104 MHz only -> predict 1992 MHz.
To re-calibrate with new device numbers: run the same workloads on the device (e.g. device-test.sh's speed rows,
or `perf stat`-timed headless runs), write them as device.tsv rows, run the workloads here with run.sh (same
binary build, same settings, --mode raw) and run this with --write ../params.json.
"""
import sys, os, json, math
import numpy as np
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import report as R

args = sys.argv[1:]
FIX = {}
NOBOUNDS = '--nobounds' in args
args = [a for a in args if a != '--nobounds']
dev, runs = args[0], args[1]
combine = 'max'; free = ['base_scale', 'l3_cyc', 'dram_ns', 'mispredict_cyc']; write = None; version = 'v1-calibrated'
const = False   # --const: a fitted per-frame constant (ms) on top: the device run's thread hand-offs, kernel time
i = 2
while i < len(args):
    if args[i] == '--combine': combine = args[i + 1]; i += 2
    elif args[i] == '--free': free = args[i + 1].split(','); i += 2
    elif args[i] == '--write': write = args[i + 1]; i += 2
    elif args[i] == '--version': version = args[i + 1]; i += 2
    elif args[i] == '--const': const = True; i += 1
    elif args[i] == '--fix': k, v = args[i + 1].split('='); FIX[k] = float(v); i += 2
    else: sys.exit('unknown arg ' + args[i])

free = [k for k in free if k not in FIX]
rows = []
for l in open(dev):
    if not l.strip() or l.startswith('#'): continue
    run, mhz, fps = l.split()[:3]
    j = os.path.join(runs, run, 'report.json')
    if not os.path.exists(j):
        print('(no run for %s: skipped)' % run); continue
    rep = json.load(open(j))
    th = sorted(rep.values(), key=lambda t: -t['totals'].get('insn', 0))
    main = rep.get('0')
    others = [t for k, t in rep.items() if k != '0']
    other = max(others, key=lambda t: t['totals'].get('insn', 0)) if others else None
    rows.append({'run': run, 'mhz': float(mhz), 'ms': 1000.0 / float(fps), 'main': main['totals'],
                 'other': other['totals'] if other else None, 'rom': run.split('-', 1)[1].replace('-thoff', '')})

base = R.load_params(None)
base.update(FIX)


def model(row, p, alpha):
    c = p.get('_const', 0.0)
    tm = R.ms_at(row['main'], p, row['mhz'])
    if row['other'] is None or combine == 'main': return tm + c
    to = R.ms_at(row['other'], p, row['mhz'])
    if combine == 'max': return max(tm, to) + c
    if combine == 'sum': return tm + to + c
    if combine == 'overlap':   # the renderer serializes a part b of the main thread, the main thread a part g of it
        b, g = alpha
        return max(tm + g * to, to + b * tm) + c
    return max(tm, to) + alpha * min(tm, to) + c


# physically plausible ranges (A55 / RK3568 / LPDDR4); --nobounds lifts them
BOUNDS = {'base_scale': (0.5, 1.6), 'l3_cyc': (5.0, 80.0), 'l3_ns': (0.0, 60.0), 'dram_ns': (60.0, 250.0),
          'mispredict_cyc': (4.0, 16.0)}
if NOBOUNDS: BOUNDS = {}


def sig(v): return 1 / (1 + math.exp(-max(-60.0, min(60.0, v))))


def unpack(x):
    p = dict(base)
    for k, v in zip(free, x[:len(free)]):
        if k in BOUNDS: lo, hi = BOUNDS[k]; p[k] = lo + (hi - lo) * sig(v)
        else: p[k] = math.exp(v)
    n = len(free)
    alpha = 0.0
    if combine == 'mix': alpha = 1 / (1 + math.exp(-x[n])); n += 1
    if combine == 'overlap': alpha = (1 / (1 + math.exp(-x[n])), 1 / (1 + math.exp(-x[n + 1]))); n += 2
    if const: p['_const'] = math.exp(x[n])
    return p, alpha


def loss(x, rs):
    p, a = unpack(x)
    return sum((model(r, p, a) / r['ms'] - 1) ** 2 for r in rs)


def nelder_mead(f, x0, step=0.5, iters=3000, tol=1e-12):
    n = len(x0); pts = [np.array(x0, float)]
    for k in range(n):
        y = np.array(x0, float); y[k] += step; pts.append(y)
    vals = [f(x) for x in pts]
    for _ in range(iters):
        o = np.argsort(vals); pts = [pts[k] for k in o]; vals = [vals[k] for k in o]
        if abs(vals[-1] - vals[0]) < tol: break
        c = sum(pts[:-1]) / n
        xr = c + (c - pts[-1]); fr = f(xr)
        if fr < vals[0]:
            xe = c + 2 * (c - pts[-1]); fe = f(xe)
            pts[-1], vals[-1] = (xe, fe) if fe < fr else (xr, fr)
        elif fr < vals[-2]:
            pts[-1], vals[-1] = xr, fr
        else:
            xc = c + 0.5 * (pts[-1] - c); fc = f(xc)
            if fc < vals[-1]: pts[-1], vals[-1] = xc, fc
            else:
                pts = [pts[0] + 0.5 * (y - pts[0]) for y in pts]; vals = [f(y) for y in pts]
    k = int(np.argmin(vals)); return pts[k], vals[k]


def fit(rs):
    best = None
    for s in ([1.0, 30, 120, 8], [0.7, 15, 60, 5], [1.5, 60, 200, 14], [1.0, 40, 250, 10]):
        st = dict(zip(['base_scale', 'l3_cyc', 'dram_ns', 'mispredict_cyc'], s)); st.setdefault('l3_ns', 5.0)
        x0 = []
        for k in free:
            v = st.get(k, base[k]) or 1.0
            if k in BOUNDS:
                lo, hi = BOUNDS[k]; f0 = min(0.95, max(0.05, (v - lo) / (hi - lo))); x0.append(math.log(f0 / (1 - f0)))
            else: x0.append(math.log(v))
        if combine == 'mix': x0.append(0.0)
        if combine == 'overlap': x0 += [0.0, -2.0]
        if const: x0.append(0.0)
        x, v = nelder_mead(lambda x: loss(x, rs), x0)
        x, v = nelder_mead(lambda x: loss(x, rs), x, step=0.1)
        if best is None or v < best[1]: best = (x, v)
    return unpack(best[0])


def errs(rs, p, a):
    return [model(r, p, a) / r['ms'] - 1 for r in rs]


p, a = fit(rows)
e = errs(rows, p, a)
print('rgds-sim calibration: %d rows, combine=%s, free=%s' % (len(rows), combine, ','.join(free)))
print('fitted: ' + ' '.join('%s=%.4g' % (k, p[k]) for k in free) + ('  alpha=%.3f' % a if combine == 'mix' else '')
      + ('  per-frame constant=%.3f ms' % p['_const'] if const else '') + ('  fixed: %s' % FIX if FIX else ''))
print('%-34s %6s %9s %9s %8s   %s' % ('run', 'MHz', 'dev ms', 'model ms', 'error', 'main/other ms'))
for r, x in zip(rows, e):
    tm = R.ms_at(r['main'], p, r['mhz']); to = R.ms_at(r['other'], p, r['mhz']) if r['other'] else 0
    print('%-34s %6d %9.2f %9.2f %+7.1f%%   %.2f / %.2f' % (r['run'], r['mhz'], r['ms'], model(r, p, a), 100 * x, tm, to))
rms = math.sqrt(sum(x * x for x in e) / len(e))
print('fit: RMS error %.1f%%, max |error| %.1f%%' % (100 * rms, 100 * max(abs(x) for x in e)))
# 1104/1992 ratio check
print('clock ratio T(1104)/T(1992): device vs model')
for run in sorted(set(r['run'] for r in rows)):
    rr = {r['mhz']: r for r in rows if r['run'] == run}
    if 1104 in rr and 1992 in rr:
        print('  %-34s %.3f  %.3f' % (run, rr[1104]['ms'] / rr[1992]['ms'], model(rr[1104], p, a) / model(rr[1992], p, a)))
# leave one ROM out
roms = sorted(set(r['rom'] for r in rows))
cv = []
for rom in roms:
    tr = [r for r in rows if r['rom'] != rom]; te = [r for r in rows if r['rom'] == rom]
    pp, aa = fit(tr); cv += errs(te, pp, aa)
print('leave-one-ROM-out (%d folds): RMS error %.1f%%, max |error| %.1f%%' % (
    len(roms), 100 * math.sqrt(sum(x * x for x in cv) / len(cv)), 100 * max(abs(x) for x in cv)))
tr = [r for r in rows if r['mhz'] == 1104]; te = [r for r in rows if r['mhz'] != 1104]
if tr and te:
    pp, aa = fit(tr); ee = errs(te, pp, aa)
    print('fit at 1104 MHz only, predict 1992 MHz: RMS error %.1f%%, max |error| %.1f%%' % (
        100 * math.sqrt(sum(x * x for x in ee) / len(ee)), 100 * max(abs(x) for x in ee)))
if write:
    out = {k: p[k] for k in ['base_scale', 'l3_cyc', 'l3_ns', 'dram_ns', 'mispredict_cyc', 'tlb_l2_cyc', 'walk_cyc', 'st_cost']}
    out['version'] = version
    out['calibration'] = {'device': os.path.basename(dev), 'combine': combine, 'rows': len(rows), 'rms_error': rms,
                          'max_error': max(abs(x) for x in e), 'alpha': a, 'frame_const_ms': p.get('_const', 0.0),
                          'free': free, 'fixed': FIX}
    json.dump(out, open(write, 'w'), indent=1)
    print('wrote ' + write)

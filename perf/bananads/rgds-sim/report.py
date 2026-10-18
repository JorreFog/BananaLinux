#!/usr/bin/env python3
"""rgds-sim report: per-frame A55 time per thread, by bucket, from two plugin runs (N1 and N2 frames).

report.py <dirA-prefix> <dirB-prefix> <frames> [--params params.json] [--json out.json] [--top 25]
  <prefix>.tsv / .threads / .map (JIT perf map, optional) as written by run.sh.
The cost model (linear; calibrate.py fits it):
  core cycles = base_scale*base + l3_cyc*(l1i_miss + ld_miss + st_cost*st_miss)   [+ l3_ns in ns, if the L3 is
                                                                                  in its own clock domain]
              + tlb_l2_cyc*(itlb_l2 + dtlb_l2) + walk_cyc*(itlb_walk + dtlb_walk)
              + mispredict_cyc*(bcond_miss + bind_miss)
  time(f)     = core cycles / f + dram_ns*(l1i_l3miss + ld_l3miss + st_cost*st_l3miss)
"""
import sys, os, json, bisect, collections

HERE = os.path.dirname(os.path.abspath(__file__))
CTR = None  # counter names from the tsv header

DEFAULT_PARAMS = {"base_scale": 1.0, "l3_cyc": 30.0, "l3_ns": 0.0, "dram_ns": 120.0, "mispredict_cyc": 8.0,
                  "tlb_l2_cyc": 2.0, "walk_cyc": 25.0, "st_cost": 0.2, "version": "uncalibrated defaults"}


def load_params(path):
    p = dict(DEFAULT_PARAMS)
    if path and os.path.exists(path):
        p.update(json.load(open(path)))
    return p


def cost(c, p):
    """c: dict counter -> value. Returns (core_cycles, dram_ns_total)."""
    g = lambda k: c.get(k, 0.0)
    core = (p["base_scale"] * g("base") + p["l3_cyc"] * (g("l1i_miss") + g("ld_miss") + p["st_cost"] * g("st_miss"))
            + p["tlb_l2_cyc"] * (g("itlb_l2") + g("dtlb_l2")) + p["walk_cyc"] * (g("itlb_walk") + g("dtlb_walk"))
            + p["mispredict_cyc"] * (g("bcond_miss") + g("bind_miss")))
    dram = (p["dram_ns"] * (g("l1i_l3miss") + g("ld_l3miss") + p["st_cost"] * g("st_l3miss"))
            + p.get("l3_ns", 0.0) * (g("l1i_miss") + g("ld_miss") + p["st_cost"] * g("st_miss")))
    return core, dram


def ms_at(c, p, mhz):
    core, dram = cost(c, p)
    return core / (mhz * 1e3) + dram * 1e-6


def components(c, p):
    """core-cycle components (for the shares), dram in cycles at 1104 MHz"""
    g = lambda k: c.get(k, 0.0)
    return {
        "base": p["base_scale"] * g("base"),
        "L1I": (p["l3_cyc"] + p.get("l3_ns", 0.0) * 1.104) * g("l1i_miss") + p["dram_ns"] * 1.104 * g("l1i_l3miss"),
        "L1D": (p["l3_cyc"] + p.get("l3_ns", 0.0) * 1.104) * (g("ld_miss") + p["st_cost"] * g("st_miss"))
               + p["dram_ns"] * 1.104 * (g("ld_l3miss") + p["st_cost"] * g("st_l3miss")),
        "iTLB": p["tlb_l2_cyc"] * g("itlb_l2") + p["walk_cyc"] * g("itlb_walk"),
        "dTLB": p["tlb_l2_cyc"] * g("dtlb_l2") + p["walk_cyc"] * g("dtlb_walk"),
        "branch": p["mispredict_cyc"] * (g("bcond_miss") + g("bind_miss")),
    }


DISPATCH = ('Execute', 'LookUpBlock', 'JumpTo', 'UpdateMode', 'HaltInterrupted', 'SetupExecutableRegion',
            'ARM_Dispatch', 'ARM_Ret', 'CompileBlock', 'Compile', 'Reset', 'ARM_RestoreContext', 'melonds_jit')
GENGIS = ('rast', 'batch', 'gx_', 'gx2', 'fog', 'edge', 'walk', 'defer', 'fused', 'Gengis', 'gengis', 'kern')


def bucket(s):
    if any(k in s for k in DISPATCH) and ('ARM' in s or 'melonds' in s or 'Compiler' in s or 'JIT' in s or 'banana' in s.lower()):
        return 'jit-runtime'
    if 'Arm64Gen' in s or 'Compiler' in s or 'ARMJIT' in s or 'bananajit' in s.lower():
        return 'jit-runtime'
    if 'Interpreter' in s or s.startswith('_ZN7melonDS14ARMInterpreter'):
        return 'interpreter'
    if ('Slow' in s or 'ARM9Read' in s or 'ARM9Write' in s or 'ARM7Read' in s or 'ARM7Write' in s or 'IORead' in s
            or 'IOWrite' in s or 'DataRead' in s or 'DataWrite' in s or 'CodeRead' in s or 'BlockTransfer' in s):
        return 'memory'
    if 'Gengis' in s or any(s.startswith(k) for k in GENGIS):
        return 'gengis'
    if 'GPU3D' in s:
        return '3d-geometry'
    if 'Renderer3D' in s or 'DepthTest' in s:
        return '3d-soft'
    if ('GPU2D' in s or 'Banana' in s or 'GPU' in s or 'SoftRenderer' in s or 'ConvertRun' in s or 'Composite' in s
            or 'CompStep' in s or 'gpu2d' in s):
        return '2d'
    if 'SPU' in s or 'blip' in s or 'audio' in s.lower():
        return 'audio'
    if s.startswith('_ZZ4main'):   # GCC 14 LTO inlines RunSlicesJIT there (core's 15:35 INFO)
        return 'scheduler/core'
    if ('RunFrame' in s or 'RunSystem' in s or 'RunTimers' in s or 'NDS' in s or 'DMA' in s or 'Schedul' in s
            or 'RTC' in s or 'Timer' in s):
        return 'scheduler/core'
    if s.startswith(('mem', 'str', '__libc', '__memcpy', '__memset', 'pthread', 'operator', '_ZdlPv', '_Znwm')):
        return 'libc'
    if 'bananads' in s:
        return 'frontend'
    return 'other'


def load_run(prefix):
    global CTR
    rows = collections.defaultdict(dict)  # vcpu -> key -> counters(list)
    kinds = {}
    with open(prefix + '.tsv') as f:
        hdr = f.readline().rstrip('\n').split('\t')
        CTR = hdr[3:]
        for l in f:
            a = l.rstrip('\n').split('\t')
            v = int(a[0]); key = (a[1], a[2])
            rows[v][key] = [int(x) for x in a[3:]]
    threads = {}
    if os.path.exists(prefix + '.threads'):
        with open(prefix + '.threads') as f:
            th = f.readline().rstrip('\n').split('\t')
            for l in f:
                a = l.rstrip('\n').split('\t')
                threads[int(a[0])] = {'tid': a[1], 'name': a[2], 'cls': dict(zip(th[3:], [int(x) for x in a[3:]]))}
    blocks = []
    if os.path.exists(prefix + '.map'):
        for l in open(prefix + '.map'):
            p = l.split()
            if len(p) >= 3:
                blocks.append((int(p[0], 16), int(p[1], 16), p[2]))
    blocks.sort()
    return rows, threads, blocks, load_maps(prefix)


_symcache = {}


def lib_symbols(path):
    """(sorted addresses, names) of a shared library's dynamic and static function symbols (nm)"""
    if path in _symcache: return _symcache[path]
    import subprocess
    syms = {}
    for flag in ('-D', ''):
        try:
            out = subprocess.run(['aarch64-linux-gnu-nm', '--defined-only'] + ([flag] if flag else []) + [path],
                                 capture_output=True, text=True, timeout=30).stdout
        except Exception:
            out = ''
        for l in out.splitlines():
            p = l.split()
            if len(p) >= 3 and p[1] in 'tTiWw':
                syms.setdefault(int(p[0], 16), p[2])
    ks = sorted(syms)
    _symcache[path] = (ks, [syms[k] for k in ks])
    return _symcache[path]


def load_maps(prefix):
    """[(start, end, base, path)] of file mappings, base = the start of the file's offset-0 mapping"""
    maps = []
    if not os.path.exists(prefix + '.maps'): return maps
    first = {}
    rows = []
    for l in open(prefix + '.maps'):
        p = l.split()
        if len(p) < 6 or not p[5].startswith('/'): continue
        a, b = (int(x, 16) for x in p[0].split('-'))
        off = int(p[2], 16)
        if off == 0 and p[5] not in first: first[p[5]] = a
        rows.append((a, b, p[5]))
    for a, b, path in rows:
        maps.append((a, b, first.get(path, a), path))
    return maps


def classify_addr(a, blocks, starts, lo, hi, maps=()):
    if blocks and lo <= a < hi:
        i = bisect.bisect_right(starts, a) - 1
        name = blocks[i][2] if i >= 0 and a < blocks[i][0] + blocks[i][1] else 'stub/unknown'
        return 'jit-code', name
    for s0, e0, base, path in maps:
        if s0 <= a < e0:
            ks, ns = lib_symbols(path)
            off = a if base < 0x10000000 else a - base     # a non-PIE executable's symbols are absolute
            i = bisect.bisect_right(ks, off) - 1
            nm = ns[i] if i >= 0 else '?'
            if '.so' not in os.path.basename(path):        # the executable itself: code qemu has no sized symbol for
                return bucket(nm), nm
            return 'libc/lib', '%s:%s' % (os.path.basename(path), nm)
    return 'libc/lib', 'lib@%x' % a


def per_frame(A, B, frames):
    """returns {vcpu: {'thread':..., 'cls':..., 'items': {(bucket, name): counters dict}}}"""
    rowsA, thA, blA, mpA = A
    rowsB, thB, blB, mpB = B
    out = {}
    for v in sorted(set(rowsB) | set(rowsA)):
        items = collections.defaultdict(lambda: collections.Counter())
        for sign, rows, bl, mp in ((-1, rowsA, blA, mpA), (1, rowsB, blB, mpB)):
            starts = [b[0] for b in bl]
            lo = min(b[0] for b in bl) - 0x100000 if bl else 0
            hi = max(b[0] + b[1] for b in bl) + 0x100000 if bl else 0
            for (kind, key), ctr in rows.get(v, {}).items():
                if kind == 'sym':
                    k = (bucket(key), key)
                else:
                    k = classify_addr(int(key, 16), bl, starts, lo, hi, mp)
                c = items[k]
                for n, x in zip(CTR, ctr):
                    c[n] += sign * x / frames
        cls = collections.Counter()
        for sign, th in ((-1, thA), (1, thB)):
            for n, x in th.get(v, {}).get('cls', {}).items():
                cls[n] += sign * x / frames
        name = thB.get(v, thA.get(v, {})).get('name', '?')
        if name in ('qemu-aarch64', '?', 'bananads'):   # unnamed threads keep the process name
            name = 'main' if v == 0 else 'thread%d' % v
        out[v] = {'name': name, 'cls': cls, 'items': items}
    return out


def total(items):
    t = collections.Counter()
    for c in items.values():
        t.update(c)
    return t


def fmt_k(x):
    return '%.1fk' % (x / 1e3)


def report(pf, p, top=25, label=''):
    lines = []
    w = lines.append
    w('rgds-sim report %s (model %s; frames per thread; ms at 1104 / 1992 MHz)' % (label, p.get('version', '?')))
    w('params: ' + ' '.join('%s=%s' % (k, v) for k, v in p.items() if k != 'version'))
    summary = []
    for v, d in pf.items():
        t = total(d['items'])
        if t['insn'] < 1000:
            continue
        core, dram = cost(t, p)
        summary.append((v, d['name'], t['insn'], core, ms_at(t, p, 1104), ms_at(t, p, 1992)))
    w('')
    w('%-4s %-16s %12s %12s %9s %9s' % ('vcpu', 'thread', 'insns/frame', 'cycles@1104', 'ms@1104', 'ms@1992'))
    for v, n, i, core, m1, m2 in summary:
        cyc1104 = m1 * 1104e3
        w('%-4d %-16s %12s %12s %9.3f %9.3f' % (v, n, fmt_k(i), fmt_k(cyc1104), m1, m2))
    for v, d in pf.items():
        t = total(d['items'])
        if t['insn'] < 1e5:
            continue
        w('')
        w('== thread %d (%s): %s insns, %.3f ms @1104, %.3f ms @1992, IPC@1104 %.2f' % (
            v, d['name'], fmt_k(t['insn']), ms_at(t, p, 1104), ms_at(t, p, 1992),
            t['insn'] / max(1, ms_at(t, p, 1104) * 1104e3)))
        comp = components(t, p)
        cs = sum(comp.values())
        w('  cycle shares @1104: ' + '  '.join('%s %.1f%%' % (k, 100 * x / cs) for k, x in comp.items()))
        g = lambda k: t.get(k, 0)
        w('  per frame: L1I miss %s (%.2f%% of fetch blocks; to DRAM %s)  L1D ld miss %s (%.2f%% of loads; DRAM %s) '
          'st miss %s (DRAM %s)  pf %s' % (fmt_k(g('l1i_miss')), 100 * g('l1i_miss') / max(1, g('insn') / 4),
                                           fmt_k(g('l1i_l3miss')), fmt_k(g('ld_miss')), 100 * g('ld_miss') / max(1, g('ld')),
                                           fmt_k(g('ld_l3miss')), fmt_k(g('st_miss')), fmt_k(g('st_l3miss')), fmt_k(g('pf'))))
        w('  iTLB L2 hit %s walk %s  dTLB L2 hit %s walk %s  branches: cond %s mispred %.2f%%, indirect+ret %s mispred %.2f%%' % (
            fmt_k(g('itlb_l2')), fmt_k(g('itlb_walk')), fmt_k(g('dtlb_l2')), fmt_k(g('dtlb_walk')), fmt_k(g('bcond')),
            100 * g('bcond_miss') / max(1, g('bcond')), fmt_k(g('bind')), 100 * g('bind_miss') / max(1, g('bind'))))
        cl = d['cls']
        ti = sum(cl.values()) or 1
        w('  classes: ' + ' '.join('%s %.1f%%' % (k, 100 * x / ti) for k, x in sorted(cl.items(), key=lambda x: -x[1]) if x / ti >= 0.001))
        buckets = collections.defaultdict(collections.Counter)
        for (b, name), c in d['items'].items():
            buckets[b].update(c)
        tot_ms = ms_at(t, p, 1104)
        w('  %-16s %10s %6s %10s %6s %8s %8s %8s %8s' % ('bucket', 'insns', '%', 'ms@1104', '%', 'L1I%', 'L1D%', 'TLB%', 'br%'))
        for b, c in sorted(buckets.items(), key=lambda x: -ms_at(x[1], p, 1104)):
            m = ms_at(c, p, 1104)
            if c['insn'] <= 0 and m <= 0:
                continue
            cp = components(c, p)
            s = sum(cp.values()) or 1
            w('  %-16s %10s %5.1f%% %10.3f %5.1f%% %7.1f%% %7.1f%% %7.1f%% %7.1f%%' % (
                b, fmt_k(c['insn']), 100 * c['insn'] / max(1, t['insn']), m, 100 * m / max(1e-9, tot_ms),
                100 * cp['L1I'] / s, 100 * cp['L1D'] / s, 100 * (cp['iTLB'] + cp['dTLB']) / s, 100 * cp['branch'] / s))
        w('  top functions / JIT blocks by modeled time @1104:')
        its = sorted(d['items'].items(), key=lambda x: -ms_at(x[1], p, 1104))
        for (b, name), c in its[:top]:
            m = ms_at(c, p, 1104)
            w('    %8.3f ms %5.1f%%  %9s insns  [%s] %s' % (m, 100 * m / max(1e-9, tot_ms), fmt_k(c['insn']), b, name[:100]))
    return '\n'.join(lines)


def to_json(pf, p):
    out = {}
    for v, d in pf.items():
        t = total(d['items'])
        if t['insn'] < 1000:
            continue
        buckets = collections.defaultdict(collections.Counter)
        for (b, name), c in d['items'].items():
            buckets[b].update(c)
        out[str(v)] = {'name': d['name'], 'totals': dict(t), 'ms1104': ms_at(t, p, 1104), 'ms1992': ms_at(t, p, 1992),
                       'buckets': {b: dict(c) for b, c in buckets.items()}, 'cls': dict(d['cls'])}
    return out


if __name__ == '__main__':
    args = sys.argv[1:]
    params = os.path.join(HERE, 'params.json')
    jout = None
    top = 25
    pos = []
    i = 0
    while i < len(args):
        if args[i] == '--params':
            params = args[i + 1]; i += 2
        elif args[i] == '--json':
            jout = args[i + 1]; i += 2
        elif args[i] == '--top':
            top = int(args[i + 1]); i += 2
        else:
            pos.append(args[i]); i += 1
    a, b, frames = pos[0], pos[1], int(pos[2])
    label = pos[3] if len(pos) > 3 else ''
    p = load_params(params)
    pf = per_frame(load_run(a), load_run(b), frames)
    print(report(pf, p, top, label))
    if jout:
        json.dump(to_json(pf, p), open(jout, 'w'), indent=1)

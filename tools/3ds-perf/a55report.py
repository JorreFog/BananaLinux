#!/usr/bin/env python3
"""a55report: a55prof's counts (a QEMU plugin) from a panda3ds-bench --marks run, as the RG DS Plus's frame budget.

    a55report.py prof.txt [--maps maps.txt] [--top 12] [--json out.json] [--compare other-prof.txt --compare-maps ...]

Each frame's AArch64 instructions are turned into Cortex-A55 milliseconds with a fixed cost model: CPI cycles per
instruction at the A55's 2.0 GHz (CPI 1.25 by default: the A55 is in order and issues two instructions a cycle at
best; emulator code, with its loads and branches, does well to average 0.8 a cycle). The model is the same for
every build, so what it compares is work, which is what the speed work changes; the handheld itself has the last
word. A frame at 60 fps has 16.7 ms.
"""
import argparse
import bisect
import collections
import json
import re
import statistics
import subprocess
import sys

GHZ = 2.0


def load_maps(path):
    """[(start, end, name)] from /proc/self/maps: a library's file name, or [anon rwx] / [anon r-x] for a JIT's code."""
    regions = []
    if not path:
        return regions
    for line in open(path):
        parts = line.split()
        start, end = (int(x, 16) for x in parts[0].split('-'))
        perms = parts[1]
        name = parts[5] if len(parts) > 5 else '[anon %s]' % perms[:3]
        regions.append((start, end, name.rsplit('/', 1)[-1]))
    regions.sort()
    return regions


def region_name(regions, addr):
    starts = [r[0] for r in regions]
    i = bisect.bisect_right(starts, addr) - 1
    if i >= 0 and regions[i][0] <= addr < regions[i][1]:
        return regions[i][2]
    return '[unmapped]'


def demangle(names):
    names = list(names)
    try:
        out = subprocess.run(['c++filt'], input='\n'.join(names), capture_output=True, text=True, check=True).stdout
        return dict(zip(names, out.split('\n')))
    except (OSError, subprocess.CalledProcessError):
        return {n: n for n in names}


CATEGORIES = [
    ('other threads (llvmpipe rasterizing = the GPU\'s work; audio)', lambda s: s == '[other threads]'),
    ('guest code (ARM11 JIT output)', lambda s: s.startswith('[jit')),
    ('GL driver (Mesa; llvmpipe here, panfrost on the handheld)', lambda s: re.search(r'libgallium|libGLES|libEGL|libGLdispatch|libLLVM|libdrm|libgbm|mesa', s)),
    ('C library (memcpy, malloc...)', lambda s: re.search(r'^\[lib\] (libc|libstdc|libm|ld-linux)', s)),
    ('other libraries', lambda s: s.startswith('[lib]')),
    ('dynarmic (translating guest code)', lambda s: re.search(r'Dynarmic|oaknut|mcl::', s)),
    ('PICA vertex shaders (shader JIT)', lambda s: re.search(r'ShaderJIT|ShaderEmitter|PICAShader|ShaderUnit|shader_rec', s)),
    ('PICA: draws, vertex fetch, commands', lambda s: re.search(r'^GPU::|PICA::|DrawAcceleration|getAcceleratedDrawInfo', s)),
    ('renderer (GL calls, caches, texture decode)', lambda s: re.search(r'RendererGL|RendererNull|Texture|SurfaceCache|OpenGL::|ColourBuffer|DepthBuffer|ShaderGen|FragmentGenerator|GLStateManager|glad', s)),
    ('audio DSP (HLE)', lambda s: re.search(r'Audio::|DSP|AAC|fdk|aac', s, re.I)),
    ('kernel, services, memory', lambda s: re.search(r'Kernel|Memory::|Service|GPUService|GSP|Scheduler|Emulator::|CPU::|MyEnvironment|KernelMemory|HIDService|APTService|DSPService|FSService', s)),
]


def categorize(name):
    for cat, test in CATEGORIES:
        if test(name):
            return cat
    return 'other emulator code'


def parse(prof_path, maps_path):
    regions = load_maps(maps_path)
    frames = collections.defaultdict(collections.Counter)  # frame -> symbol -> insns
    scene_starts = []  # (frame, order)
    current = None  # frame being run
    pending = collections.Counter()
    setup = collections.Counter()
    for line in open(prof_path):
        if line.startswith('MARK '):
            _, mid, value, _total = line.split()
            mid, value = int(mid), int(value)
            target = frames[current] if current is not None else setup
            target.update(pending)
            pending = collections.Counter()
            if mid == 4:
                current = value
            elif mid == 3:
                scene_starts.append(value)
            elif mid == 5:
                current = None
            continue
        count, name = line.rstrip('\n').split('\t', 1)
        pending[name] += int(count)
    # names: [nosym 0x...] -> its library or the JIT's anonymous executable memory
    rename = {}
    all_names = set()
    for c in frames.values():
        all_names.update(c)
    bare = set()
    for n in all_names:
        m = re.match(r'(@\d+ )?(.*)$', n)
        bare.add(m.group(2))
    resolved = {}
    for n in bare:
        m = re.match(r'\[nosym 0x([0-9a-f]+)\]', n)
        if m:
            r = region_name(regions, int(m.group(1), 16)) if regions else 'unknown'
            resolved[n] = '[jit %s]' % r if r.startswith('[anon') or r == '[unmapped]' else '[lib] ' + r
    resolved.update(demangle(n for n in bare if n not in resolved))
    for n in all_names:
        m = re.match(r'(@\d+ )?(.*)$', n)
        rename[n] = (m.group(1) or '') + resolved.get(m.group(2), m.group(2))
    out = {}
    for f, c in frames.items():
        merged = collections.Counter()
        for n, v in c.items():
            merged[rename.get(n, n)] += v
        out[f] = merged
    return out, sorted(scene_starts)


def categorize_all(counter):
    cats = collections.Counter()
    for n, v in counter.items():
        cats[categorize(n)] += v
    return cats


def thread_name(counter):
    """what a thread is, from where its instructions go"""
    text = ' '.join(n for n, _ in counter.most_common(30))
    if re.search(r'RendererGL|GPU::|PICA|GPUWorker', text):
        return 'GPU thread (renderer + GL driver: a core of its own on the handheld)'
    if re.search(r'lp_|llvmpipe|libgallium|\[jit', text):
        return 'llvmpipe rasterizer (the Mali\'s work on the handheld)'
    if re.search(r'Audio|miniaudio|ma_', text):
        return 'audio'
    return 'other'


def scene_of(frame, starts, names):
    i = bisect.bisect_right(starts, frame) - 1
    return names[i] if i >= 0 else 'boot'


def summarize(prof, maps, scene_names, cpi, top):
    frames, starts = parse(prof, maps)
    names = scene_names[:len(starts)] + ['scene%d' % i for i in range(len(scene_names), len(starts))]
    by_scene = collections.OrderedDict()
    for f in sorted(frames):
        by_scene.setdefault(scene_of(f, starts, names), []).append(frames[f])
    result = collections.OrderedDict()
    for scene, flist in by_scene.items():
        totals = [sum(v for n, v in c.items() if n != '[other threads]' and not n.startswith('@')) for c in flist]
        ms = [t * cpi / (GHZ * 1e6) for t in totals]
        cats = collections.Counter()
        syms = collections.Counter()
        threads = collections.defaultdict(collections.Counter)  # the other threads: number -> function -> count
        for c in flist:
            for n, v in c.items():
                m = re.match(r'@(\d+) (.*)$', n)
                if m:
                    threads[int(m.group(1))][m.group(2)] += v
                    continue
                cats[categorize(n)] += v
                syms[n] += v
        nf = len(flist)
        result[scene] = {
            'frames': nf,
            'insns_per_frame': statistics.mean(totals),
            'ms_p50': sorted(ms)[len(ms) // 2],
            'ms_mean': statistics.mean(ms),
            'ms_p95': sorted(ms)[min(len(ms) - 1, int(0.95 * (len(ms) - 1) + 0.5))],
            'ms_max': max(ms),
            'categories': {k: v / nf for k, v in cats.most_common()},
            'top': [(n, v / nf) for n, v in syms.most_common(top)],
            'threads': {k: {'name': thread_name(t), 'insns_per_frame': sum(t.values()) / nf,
                            'categories': {cat: v / nf for cat, v in categorize_all(t).most_common()},
                            'top': [(n, v / nf) for n, v in t.most_common(6)]} for k, t in sorted(threads.items())},
        }
    return result


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('prof')
    ap.add_argument('--maps')
    ap.add_argument('--scenes', default='cpu,3d,2d,mixed,fill', help='the ROM\'s scene names, in order')
    ap.add_argument('--cpi', type=float, default=1.25)
    ap.add_argument('--top', type=int, default=12)
    ap.add_argument('--json')
    ap.add_argument('--compare', help='a second profile (e.g. the baseline) to compare with')
    ap.add_argument('--compare-maps')
    args = ap.parse_args()
    scenes = args.scenes.split(',')
    res = summarize(args.prof, args.maps, scenes, args.cpi, args.top)
    base = summarize(args.compare, args.compare_maps, scenes, args.cpi, args.top) if args.compare else None
    print('Cortex-A55 @ %.1f GHz, CPI %.2f: 16.7 ms is a 60 fps frame' % (GHZ, args.cpi))
    print('(the emulator\'s own thread; other threads are listed per scene)')
    hdr = '%-8s %6s %14s %9s %9s %9s %9s' % ('scene', 'frames', 'insns/frame', 'mean ms', 'p50 ms', 'p95 ms', 'max ms')
    if base:
        hdr += '  %12s %8s' % ('baseline ms', 'speedup')
    print(hdr)
    for scene, r in res.items():
        line = '%-8s %6d %14.0f %9.2f %9.2f %9.2f %9.2f' % (scene, r['frames'], r['insns_per_frame'], r['ms_mean'], r['ms_p50'], r['ms_p95'], r['ms_max'])
        if base and scene in base:
            b = base[scene]['ms_mean']
            line += '  %12.2f %7.2fx' % (b, b / r['ms_mean'] if r['ms_mean'] else 0)
        print(line)
    for scene, r in res.items():
        if scene == 'boot':
            continue
        print('\n== %s: where a frame goes (est. A55 ms)' % scene)
        for cat, v in r['categories'].items():
            bv = base[scene]['categories'].get(cat, 0) if base and scene in base else None
            print('  %7.2f ms  %s%s' % (v * args.cpi / (GHZ * 1e6), cat, '' if bv is None else '   (baseline %.2f)' % (bv * args.cpi / (GHZ * 1e6))))
        print('  top functions:')
        for n, v in r['top']:
            print('  %7.2f ms  %s' % (v * args.cpi / (GHZ * 1e6), n[:150]))
        for k, t in r.get('threads', {}).items():
            if t['insns_per_frame'] * args.cpi / (GHZ * 1e6) < 0.05:
                continue
            print('  -- thread %d, %s: %.2f ms' % (k, t['name'], t['insns_per_frame'] * args.cpi / (GHZ * 1e6)))
            for cat, v in list(t['categories'].items())[:4]:
                print('     %7.2f ms  %s' % (v * args.cpi / (GHZ * 1e6), cat))
            for n, v in t['top'][:4]:
                print('     %7.2f ms    %s' % (v * args.cpi / (GHZ * 1e6), n[:120]))
    if args.json:
        json.dump({'result': res, 'baseline': base}, open(args.json, 'w'), indent=1)


if __name__ == '__main__':
    main()

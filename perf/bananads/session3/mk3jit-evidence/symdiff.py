#!/usr/bin/env python3
# symdiff.py <qc dir A> <qc dir B> [n]: per-symbol instructions a frame (1650-2150, vcpu 0), B - A, LTO suffixes normalised
import sys, re, collections
def load(d):
    out = collections.Counter()
    for f, sg in (('f1650.tsv', -1), ('f2150.tsv', 1)):
        for l in open(d + '/' + f):
            v, k, key, n, m, b = l.rstrip('\n').split('\t')
            if v == '0':
                out[re.sub(r'\.(localalias|lto_priv\.\d+|constprop\.\d+|isra\.\d+|part\.\d+)', '', key) if k == 'sym' else 'JIT code'] += sg * int(n)
    return out
a = load(sys.argv[1]); b = load(sys.argv[2]); n = int(sys.argv[3]) if len(sys.argv) > 3 else 12
d = collections.Counter(b); d.subtract(a)
gen = ('line_px', 'rast_', 'gx_', 'f_run', 'ClipAgainstPlane')
items = [(k, v) for k, v in d.items() if v and not k.startswith(gen) and 'ClipAgainstPlane' not in k]
for k, v in sorted(items, key=lambda x: x[1])[:n]: print('%8.1fk %s' % (v / 500e3, k[:100]))
print('  ...')
for k, v in sorted(items, key=lambda x: -x[1])[:5]: print('%8.1fk %s' % (v / 500e3, k[:100]))

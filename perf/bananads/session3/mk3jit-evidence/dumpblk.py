#!/usr/bin/env python3
# dumpblk.py <code dump> <map> <name or hex addr> [qc dir]: disassemble one block (last record), with absolute addresses
# and, given the qc dir, each qemu TB's executions per frame (1650-2150) at its start
import sys, struct, subprocess, tempfile, os, re, collections
data = open(sys.argv[1], 'rb').read(); recs = {}
i = 0
while i + 12 <= len(data):
    at, n = struct.unpack_from('<QI', data, i); i += 12; recs[at] = data[i:i + n]; i += n
names = {}
for l in open(sys.argv[2]):
    a, s, n = l.split()[:3]; names[n] = int(a, 16)
k = sys.argv[3]; at = names[k] if k in names else int(k, 16)
cnt = collections.Counter()
if len(sys.argv) > 4:
    for f, sg in (('f1650.tsv', -1), ('f2150.tsv', 1)):
        for l in open(sys.argv[4] + '/' + f):
            v, kk, key, n, m, b = l.rstrip('\n').split('\t')
            if v == '0' and kk == 'addr': cnt[int(key, 16)] += sg * int(n)
OD = '/home/user/perf/toolchain/aarch64--glibc--stable-2025.08-1/bin/aarch64-linux-objdump'
with tempfile.NamedTemporaryFile(delete=False) as f: f.write(recs[at]); fn = f.name
out = subprocess.run([OD, '-D', '-b', 'binary', '-m', 'aarch64', '--no-show-raw-insn', '--adjust-vma=0x%x' % at, fn], capture_output=True, text=True).stdout
os.unlink(fn)
for l in out.splitlines():
    m = re.match(r'^\s+([0-9a-f]+):\t(.*)', l)
    if not m: continue
    a = int(m.group(1), 16); c = cnt.get(a)
    print(('%8.1fk ' % (c / 500 / 1e3) if c else ' ' * 10) + '%x: %s' % (a, m.group(2)))

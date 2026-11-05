import struct, sys, subprocess, re, os, tempfile, collections
data = open(sys.argv[1], 'rb').read(); names = {}
for l in open(sys.argv[2]):
    a, s, n = l.split()[:3]; names[int(a, 16)] = n
OD = '/home/user/perf/toolchain/aarch64--glibc--stable-2025.08-1/bin/aarch64-linux-objdump'
blocks=[]; i=0
while i + 12 <= len(data):
    at, n = struct.unpack_from('<QI', data, i); i += 12
    nm = names.get(at, '?')
    if nm.startswith(('mj9_', 'mj7_')): blocks.append(data[i:i+n])
    i += n
buf=b''.join(blocks)
with tempfile.NamedTemporaryFile(delete=False) as f: f.write(buf); fn = f.name
out = subprocess.run([OD, '-D', '-b', 'binary', '-m', 'aarch64', '--no-show-raw-insn', fn], capture_output=True, text=True).stdout
ins=[re.sub(r'\s*//.*','',l.split('\t',1)[1].strip()) for l in out.splitlines() if re.match(r'^\s+[0-9a-f]+:\t', l)]
def norm(t): 
    t=re.sub(r'0x[0-9a-f]+','N',t); t=re.sub(r'#-?\d+','#N',t); return t
N=[norm(t) for t in ins]
print(len(blocks),'blocks',len(buf),'bytes',len(ins),'insns')
for k in (2,3,4,5):
    c=collections.Counter(tuple(N[j:j+k]) for j in range(len(N)-k))
    print('--- %d-grams'%k)
    for s,v in c.most_common(12): print('%6d  %s'%(v,' ; '.join(s)))
pats={'dyn miss':'cmp\tw1, #0x2','nz cset':'cset\tw0, eq','mrs':'mrs\tx17, nzcv','entry movk':'movk\tx26, #N, lsl #32'}
for k,p in pats.items(): print(k, sum(1 for t in ins if norm(t)==norm(p)))

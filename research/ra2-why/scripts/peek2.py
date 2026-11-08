import glob

for p in sorted(glob.glob('/path/to/user/ra2/analysis/extracted/unnamed/multimd*')):
    d = open(p, 'rb').read(16)
    tail = p.split('_')[-1]
    print(tail, d[:8].hex(), repr(d[:12]))

print('--- language mix binaries ---')
for p in glob.glob('/path/to/user/ra2/analysis/extracted/unnamed/language*'):
    d = open(p, rb if False else 'rb').read(16)
    print(p.split('/')[-1], d[:8].hex(), repr(d[:12]))

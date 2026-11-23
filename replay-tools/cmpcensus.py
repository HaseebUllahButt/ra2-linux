#!/usr/bin/env python3
"""cmpcensus.py ORIG_CENSUS NATIVE_CENSUS [MAXDF=3] : compare CENSUS lines by game frame (nearest sample within MAXDF frames).
Fields compared per house: country cash structures vehicles infantry aircraft miners types. Prints first mismatch and a summary."""
import sys, re, collections
def load(p):
    d = collections.defaultdict(dict)
    for l in open(p, errors='replace'):
        if not l.startswith('CENSUS'): continue
        kv = dict(x.split('=', 1) for x in l.split()[1:] if '=' in x)
        d[int(kv['frame'])][int(kv['house'])] = {k: kv.get(k, '') for k in ('country', 'cash', 'structures', 'vehicles', 'infantry', 'aircraft', 'miners', 'types')}
    return d
O, N = load(sys.argv[1]), load(sys.argv[2]); mx = int(sys.argv[3]) if len(sys.argv) > 3 else 3
of = sorted(O)
exact = sum(1 for f in N if f in O)
bad = []; ok = 0; skipped = 0
for g in sorted(N):
    f = min(of, key=lambda x: abs(x - g))
    if abs(f - g) > mx: skipped += 1; continue
    diffs = []
    for h in sorted(N[g]):
        if h not in O[f]: diffs.append((h, 'house missing')); continue
        for k, v in N[g][h].items():
            if O[f][h][k] != v: diffs.append((h, k, O[f][h][k], v))
    if diffs: bad.append((g, f, diffs))
    else: ok += 1
print('orig samples %d, native samples %d, exact same-frame pairs %d; compared %d (nearest within %d frames): identical %d, different %d, skipped %d' % (len(O), len(N), exact, ok + len(bad), mx, ok, len(bad), skipped))
for g, f, d in bad[:12]: print('native frame', g, 'orig frame', f, d[:6])

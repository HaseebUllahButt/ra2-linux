#!/usr/bin/env python3
"""entrystats.py: where the entries of a new list came from, and what happened to each seed list.
usage: entrystats.py <old-list> <new-list> <find-log.tsv> [TAG=FILE]...
For each seed list: how many are in the old list, added, rejected (by reason), or not tried
(and why: inside reached code where a plain instruction runs into it, splits an instruction,
padding, jump table, outside .text)."""
import collections, os, re, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import lift as L
import coverage as C

def main():
    old = C.load_entries(sys.argv[1]); new = C.load_entries(sys.argv[2])
    log = {}
    for l in open(sys.argv[3]):
        a, s, v, w = l.rstrip('\n').split('\t')
        log[int(a, 16)] = (set(s.split(',')), v, w)
    seeds = {}
    for x in sys.argv[4:]:
        tag, fn = x.split('=', 1)
        seeds[tag] = C.load_entries(fn)
    img = L.Image(L.EXE)
    added = new - old
    print('old entries %d, new list %d, added %d, old entries dropped %d' % (len(old), len(new), len(added), len(old - new)))
    src = collections.Counter()
    for a in added:
        s = log.get(a, ({'?'}, '', ''))[0]
        for t in s:
            src[t] += 1
    print('added entries by source (one entry can have several sources):')
    for t, n in src.most_common():
        print('  %-6s %6d' % (t, n))
    only = collections.Counter(','.join(sorted(log.get(a, ({'?'},))[0])) for a in added)
    print('added entries by exact source set:')
    for t, n in only.most_common():
        print('  %-24s %6d' % (t, n))
    res = C.discover_all(new)
    cov = C.Cover(img, res)
    for tag, ts in seeds.items():
        c = collections.Counter()
        for t in ts:
            if t in old:
                c['already in old list'] += 1
            elif t in new:
                c['added'] += 1
            elif t in log and log[t][1] == 'rejected':
                c['rejected: ' + re.sub('[0-9a-f]{8}', 'X', log[t][2]).replace('final check: ', '')] += 1
            elif not (cov.lo <= t < cov.hi):
                c['not tried: outside .text'] += 1
            else:
                k = cov.m[t - cov.lo]
                if k == 2:
                    c['not tried: inside a jump table'] += 1
                elif k == 1 and not cov.start[t - cov.lo]:
                    c['not tried: splits an instruction of reached code'] += 1
                elif k == 1:
                    c['not tried: label inside reached code (code runs into it)'] += 1
                else:
                    c['not tried: padding'] += 1
        print('seed list %s: %d' % (tag, len(ts)))
        for k, n in sorted(c.items(), key=lambda x: -x[1]):
            print('  %6d  %s' % (n, k))

if __name__ == '__main__':
    main()

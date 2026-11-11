#!/usr/bin/env python3
"""liftcheck.py: run lift.lift() on every entry of a list (no files written).
usage: liftcheck.py <entry-list> [--new-only OLD-LIST] [--fails FILE]
Prints how many lift and fail, failures grouped by reason. Uses 2 processes (RC_PROCS)."""
import collections, multiprocessing, os, re, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import lift as L
from capstone import Cs, CS_ARCH_X86, CS_MODE_32

PROCS = int(os.environ.get('RC_PROCS', '2'))
_G = {}

def _init(entries):
    _G['img'] = L.Image(L.EXE)
    md = Cs(CS_ARCH_X86, CS_MODE_32); md.detail = True
    _G['md'] = md
    _G['entries'] = entries

def one(a):
    try:
        src, f = L.lift(_G['img'], _G['md'], a, _G['entries'] | {a})
        traps = sum(1 for v in f.bad.values() if v.startswith('falls into next function'))
        return a, True, '', traps
    except L.LiftError as ex:
        return a, False, str(ex), 0
    except Exception as ex:
        return a, False, 'crash: %r' % ex, 0

def reason(e):
    e = re.sub(r'^[0-9a-f]{8} \S+ [^:]*: ', '', e)
    e = re.sub(r'[0-9a-f]{8}', 'X', e)
    e = re.sub(r'reads flags \w+', 'reads flags', e)
    return e

def main():
    path = sys.argv[1]
    entries = set(int(x, 16) for x in open(path).read().split())
    todo = sorted(entries)
    old = set()
    if '--new-only' in sys.argv:
        old = set(int(x, 16) for x in open(sys.argv[sys.argv.index('--new-only') + 1]).read().split())
    with multiprocessing.get_context('fork').Pool(PROCS, _init, (frozenset(entries),)) as p:
        out = p.map(one, todo, chunksize=64)
    for label, sel in (('all', lambda a: True), ('old', lambda a: a in old), ('new', lambda a: a not in old)):
        rows = [r for r in out if sel(r[0])]
        if not old and label != 'all':
            continue
        ok = sum(1 for r in rows if r[1])
        print('%-4s %6d entries: %6d lift, %4d fail' % (label, len(rows), ok, len(rows) - ok))
        c = collections.Counter(reason(r[2]) for r in rows if not r[1])
        for k, n in c.most_common():
            print('       %5d  %s' % (n, k))
    tr = sum(1 for r in out if r[1] and r[3])
    print('lifted functions with a trap where code runs into the next entry: %d' % tr)
    if '--fails' in sys.argv:
        with open(sys.argv[sys.argv.index('--fails') + 1], 'w') as f:
            for a, ok, e, _ in out:
                if not ok:
                    f.write('%08x\t%s\t%s\n' % (a, 'new' if old and a not in old else 'old', e))

if __name__ == '__main__':
    main()

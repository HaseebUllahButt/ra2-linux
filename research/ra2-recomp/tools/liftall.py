#!/usr/bin/env python3
"""liftall.py: lift every entry (RC_ENTRIES, default ra2-1to1 all-addresses.txt) into build/all/lift
in parallel and write build/all/status.json (same format as mktest.py, so its cache is reused).
usage: liftall.py [jobs]"""
import hashlib, json, os, sys, time
from multiprocessing import Pool
HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import lift as L
from capstone import Cs, CS_ARCH_X86, CS_MODE_32
LB = ROOT + '/build/all'
_img = _md = _ent = None
def init():
    global _img, _md, _ent
    _img = L.Image(L.EXE); _md = Cs(CS_ARCH_X86, CS_MODE_32); _md.detail = True
    _ent = set(int(x, 16) for x in open(L.ENTRIES).read().split())
def one(a):
    try:
        src, f = L.lift(_img, _md, a, _ent | {a})
        fp = '%s/lift/f_%08x.c' % (LB, a)
        if not (os.path.exists(fp) and open(fp).read() == src):
            open(fp, 'w').write(src)
        return a, {'ok': True, 'insns': len(f.insns), 'tables': len(f.tables)}
    except L.LiftError as ex:
        return a, {'ok': False, 'error': str(ex)}
if __name__ == '__main__':
    jobs = int(sys.argv[1]) if len(sys.argv) > 1 else os.cpu_count()
    os.makedirs(LB + '/lift', exist_ok=True)
    for d in (ROOT + '/build', LB, LB + '/lift'): open(d + '/.trackerignore', 'a').close()
    ents = sorted(set(int(x, 16) for x in open(L.ENTRIES).read().split()))
    t0 = time.time()
    with Pool(jobs, init) as p:
        st = dict(p.map(one, ents, chunksize=32))
    lh = hashlib.sha256(open(HERE + '/lift.py', 'rb').read()).hexdigest()
    json.dump({'lift_hash': lh, 'status': {'%08x' % a: v for a, v in sorted(st.items())}}, open(LB + '/status.json', 'w'))
    bad = [(a, v['error']) for a, v in st.items() if not v['ok']]
    print('lifted %d of %d in %.0fs' % (len(st) - len(bad), len(st), time.time() - t0))
    for a, e in bad[:30]: print('  FAIL %08x %s' % (a, e))

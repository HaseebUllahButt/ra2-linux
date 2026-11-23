#!/usr/bin/env python3
"""mkdll.py: build rc.dll (all lifted functions + runtime + shim) and rclaunch.exe.
usage: mkdll.py [out-dir]      (default build/win)
Input: lifted sources in build/all/lift and build/all/status.json (made by mktest.py / run_groups.sh).
Writes <out>/rc.dll, <out>/rclaunch.exe, <out>/funcs.txt (address, patchable, size, instructions).
Env: RC_JOBS (compile jobs, default 6), RC_BATCH (functions per compile unit, default 150)."""
import json, os, subprocess, sys, time, hashlib
from multiprocessing import Pool
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import lift as L
from capstone import Cs, CS_ARCH_X86, CS_MODE_32

WCC = [HERE + '/wcc']
CFLAGS = ['-O1', '-std=gnu11', '-fno-strict-aliasing', '-fwrapv', '-w', '-mno-sse', '-mno-sse2',
          '-ffp-contract=off', '-frounding-math', '-g0', '-I' + ROOT + '/rt'] + os.environ.get('RC_CFLAGS', '').split()

_img = _md = _entries = None
def _init(entries):
    global _img, _md, _entries
    _img = L.Image(L.EXE); _md = Cs(CS_ARCH_X86, CS_MODE_32); _md.detail = True; _entries = entries
def _disc(a):
    f = L.Func(_img, _md, a, _entries | {a})
    try:
        f.discover()
    except L.LiftError:
        return a, [], [], []
    return a, [(x, i.size) for x, i in f.insns.items()], sorted(f.targets), f.table_data

def patch_info(addrs, entries):
    """patchable: the 5 bytes at the entry belong only to this function, no jump lands
    inside them, and no other function's instructions use them."""
    with Pool(int(os.environ.get('RC_JOBS', '6')), _init, (entries,)) as p:
        res = p.map(_disc, sorted(entries), chunksize=64)
    win = {b: set() for a in addrs for b in range(a, a + 5)}
    tgt, ext = set(), {}
    tdata = sorted(set(r for x in res for r in x[3]))
    for a, ins, targets, _ in res:
        tgt.update(targets)
        ext[a] = (sum(s for x, s in ins), len(ins))
        for x, s in ins:
            for b in range(x, x + s):
                if b in win:
                    win[b].add(a)
    ent = set(entries)
    out = {}
    intab = []
    for a in addrs:
        ok = all(win[b] == {a} for b in range(a, a + 5))
        ok = ok and not any(b in ent or b in tgt for b in range(a + 1, a + 5))
        if any(lo < a + 5 and a < hi for lo, hi in tdata):   # inside jump-table data: never patch
            ok = False; intab.append(a)
        out[a] = (ok, ext.get(a, (0, 0)))
    if intab:
        print('%d entries lie in jump-table data (not patched): %s' % (len(intab), ' '.join('%08x' % x for x in intab[:20])))
    return out

def main():
    out = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else ROOT + '/build/win')
    lb = ROOT + '/build/all'
    st = json.load(open(lb + '/status.json'))
    addrs = sorted(int(a, 16) for a, v in st['status'].items() if v['ok'])
    entries = set(int(x, 16) for x in open(L.ENTRIES).read().split()) | set(addrs)
    os.makedirs(out + '/obj', exist_ok=True)
    t0 = time.time()
    pi = patch_info(addrs, entries)
    npatch = sum(1 for a in addrs if pi[a][0])
    print('patch info: %d functions, %d patchable (%.1fs)' % (len(addrs), npatch, time.time() - t0), flush=True)
    with open(out + '/funcs.txt', 'w') as f:
        for a in addrs:
            f.write('%08x %d %d %d\n' % (a, pi[a][0], pi[a][1][0], pi[a][1][1]))
    # unity batches
    bs = int(os.environ.get('RC_BATCH', '150'))
    jobs = int(os.environ.get('RC_JOBS', '6'))
    batches = [addrs[i:i + bs] for i in range(0, len(addrs), bs)]
    hdr = max(os.path.getmtime(ROOT + '/rt/rc.h'), os.path.getmtime(__file__))
    todo, objs = [], []
    for k, b in enumerate(batches):
        src = '%s/obj/u%03d.c' % (out, k)
        text = ''.join('#include "%s/lift/f_%08x.c"\n' % (lb, a) for a in b)
        if not os.path.exists(src) or open(src).read() != text:
            open(src, 'w').write(text)
        o = src[:-2] + '.o'
        objs.append(o)
        newest = max([os.path.getmtime(src), hdr] + [os.path.getmtime('%s/lift/f_%08x.c' % (lb, a)) for a in b])
        if not (os.path.exists(o) and os.path.getmtime(o) > newest):
            todo.append((src, o))
    t1 = time.time()
    running, failed = [], []
    for s, o in todo:
        while len(running) >= jobs:
            p, so = running.pop(0)
            if p.wait(): failed.append(so)
        running.append((subprocess.Popen(['nice'] + WCC + CFLAGS + ['-c', s, '-o', o]), s))
    for p, so in running:
        if p.wait(): failed.append(so)
    if failed:
        print('compile failed: %s' % ' '.join(failed)); sys.exit(1)
    print('compiled %d units in %.1fs' % (len(todo), time.time() - t1), flush=True)
    g = ['#include "rc.h"']
    for a in addrs:
        g.append('void f_%08x(void);' % a)
    g.append('struct rc_fn { u32 addr; void (*fn)(void); };')
    g.append('const struct rc_fn rc_table[] = {')
    g += ['  {0x%08xu, f_%08x},' % (a, a) for a in addrs]
    g.append('  {0, 0}};')
    g.append('const int rc_table_n = %d;' % len(addrs))
    g.append('const u8 rc_patchable[] = {%s};' % ','.join('1' if pi[a][0] else '0' for a in addrs))
    open(out + '/glue.c', 'w').write('\n'.join(g) + '\n')
    rt = [ROOT + '/rt/rc_rt.c', ROOT + '/rt/bridge.S', ROOT + '/shim/rc_win.c', out + '/glue.c']
    t2 = time.time()
    subprocess.run(WCC + CFLAGS + ['-shared', '-o', out + '/rc.dll'] + rt + objs, check=True)
    subprocess.run(WCC + ['-O1', '-o', out + '/rclaunch.exe', ROOT + '/shim/launch.c'], check=True)
    print('linked rc.dll (%d functions) in %.1fs' % (len(addrs), time.time() - t2))

if __name__ == '__main__':
    main()

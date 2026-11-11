#!/usr/bin/env python3
"""mktest.py: lift a set of functions (plus their direct callees) and build a
differential test binary with the 1:1 track's difftest runtime (read-only use).
usage: mktest.py <build-dir> <fixture.c|-> <closure-depth> <addr>...
Writes <build-dir>/test and <build-dir>/lift.jsonl. Run: cd <build-dir> && DIFF_STRICT=1 ./test <game.exe>"""
import json, os, re, subprocess, sys, time
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
T = '/path/to/user/Projects/ra2/ra2-1to1'
sys.path.insert(0, HERE)
import lift as L
from capstone import Cs, CS_ARCH_X86, CS_MODE_32

CC = re.compile(r'\b(__thiscall|__fastcall|__stdcall|__cdecl)\b')

def sig_row(a):
    """Same rules as ra2-1to1/tools/gen_difftable.py, read from gen/all."""
    p = '%s/gen/all/F_%s.c' % (T, a)
    if not os.path.exists(p):
        return None
    t = open(p).read()
    sm = re.search(r'^(.*?)\bF_%s\((.*?)\)\s*\n\{' % a, t, re.M | re.S)
    if not sm:
        return None
    ret, params = sm.group(1).strip().split('\n')[-1].strip(), sm.group(2).strip()
    plist = [] if params in ('', 'void') else [x.strip() for x in params.split(',')]
    raw = '%s/inputs/signatures/FUN_%s.c' % (T, a)
    cc = '__cdecl'
    if os.path.exists(raw):
        c = CC.search(open(raw).read(2000))
        if c:
            cc = c.group(1)
    nreg = {'__thiscall': 1, '__fastcall': 2}.get(cc, 0)
    fret = 1 if ret in ('float10', 'double', 'float') else 0
    rb = ret.replace('const', '').strip()
    rsz = 0 if rb == 'void' else 1 if rb in ('char', 'bool', 'byte', 'undefined1', 'undefined', 'uchar') else 2 if rb in ('short', 'ushort', 'undefined2', 'wchar_t') else 4
    wide = any(re.search(r'\b(double|float|float10|longlong|ulonglong|undefined8)\b', x) and '*' not in x for x in plist)
    agg = ret in ('CellStruct', 'CoordStruct', 'Point2D', 'Point3D', 'RectangleStruct', 'GUID', 'RECT')
    return dict(n=len(plist), nreg=min(nreg, len(plist)), fret=fret, rsize=rsz, wide=wide or agg or len(plist) > 16 or ret in ('longlong', 'ulonglong', 'undefined8'))

def main():
    bd, fixture, depth = sys.argv[1], sys.argv[2], int(sys.argv[3])
    tests = [x.lower() for x in sys.argv[4:]]
    # depth -1: lift every known function into a shared folder (build/all/lift)
    lb = os.path.join(os.path.dirname(os.path.abspath(bd)), 'all') if depth < 0 else bd
    os.makedirs(bd, exist_ok=True)
    os.makedirs(lb + '/lift', exist_ok=True)
    img = L.Image(L.EXE)
    md = Cs(CS_ARCH_X86, CS_MODE_32); md.detail = True
    entries = set(int(x, 16) for x in open(L.ENTRIES).read().split())
    lifted, status = {}, {}
    frontier = [int(a, 16) for a in tests]
    import hashlib
    lh = hashlib.sha256(open(os.path.join(HERE, 'lift.py'), 'rb').read()).hexdigest()
    cache = lb + '/status.json'
    t0 = time.time()
    if depth < 0 and os.path.exists(cache) and json.load(open(cache))['lift_hash'] == lh:
        c = json.load(open(cache))
        status = {int(a, 16): v for a, v in c['status'].items()}
        lifted = {a: True for a, v in status.items() if v['ok']}
        frontier = []; depth = -2
    elif depth < 0:
        frontier = sorted(entries | set(frontier)); depth = 0
    for d in range(max(depth, -1) + 1):
        nxt = []
        for a in frontier:
            if a in status:
                continue
            try:
                src, f = L.lift(img, md, a, entries | {a})
                fp = '%s/lift/f_%08x.c' % (lb, a)
                if not (os.path.exists(fp) and open(fp).read() == src):
                    open(fp, 'w').write(src)
                status[a] = {'ok': True, 'insns': len(f.insns), 'tables': len(f.tables)}
                lifted[a] = f
                nxt += [c for c in f.calls if img.text_lo <= c < img.text_hi]
            except L.LiftError as ex:
                status[a] = {'ok': False, 'error': str(ex)}
        frontier = nxt
    lift_sec = time.time() - t0
    # compile lifted functions, two at a time
    cf = ['gcc', '-m32', '-O1', '-std=gnu11', '-fno-strict-aliasing', '-fwrapv', '-w', '-mfpmath=387', '-frounding-math',
          '-fno-pie', '-no-pie', '-I' + ROOT + '/rt']
    t1 = time.time()
    srcs = ['%s/lift/f_%08x.c' % (lb, a) for a in sorted(lifted)]
    objs = [s[:-2] + '.o' for s in srcs]
    hdr = os.path.getmtime(ROOT + '/rt/rc.h')
    todo = [(s, o) for s, o in zip(srcs, objs) if not (os.path.exists(o) and os.path.getmtime(o) > max(os.path.getmtime(s), hdr))]
    running = []
    for s, o in todo:
        while len(running) >= 2:
            running.pop(0).wait()
        running.append(subprocess.Popen(['nice'] + cf + ['-c', s, '-o', o]))
    for p in running:
        p.wait()
    bad = [o for o in objs if not os.path.exists(o)]
    for o in bad:
        a = int(os.path.basename(o)[2:10], 16)
        status[a] = {'ok': False, 'error': 'gcc failed'}
        del lifted[a]
    objs = [o for o in objs if os.path.exists(o)]
    if bad:
        print('compile failed for %d functions' % len(bad))
    cc_sec = time.time() - t1
    if lb != bd:
        json.dump({'lift_hash': lh, 'status': {'%08x' % a: v for a, v in status.items()}}, open(cache, 'w'))
    rows = []
    for a in tests:
        r = sig_row(a)
        st = status[int(a, 16)]
        if not st['ok']:
            print('CANNOT LIFT %s: %s' % (a, st['error'])); continue
        if r is None or r['wide']:
            print('NO TEST ROW %s' % a); continue
        rows.append((a, r))
    g = ['#include "rc.h"', 'u32 rc_stack_top(void);']
    for a in sorted(lifted):
        g.append('void f_%08x(void);' % a)
    g.append('struct rc_fn { u32 addr; void (*fn)(void); };')
    g.append('const struct rc_fn rc_table[] = {')
    for a in sorted(lifted):
        g.append('  {0x%08xu, f_%08x},' % (a, a))
    g.append('  {0, 0}};')
    g.append('const int rc_table_n = %d;' % len(lifted))
    g.append('''
static void rc_enter(u32 addr, int n, int nreg, const u32 *args)
{
    u32 sp = rc_stack_top() & ~15u;
    for (int i = n - nreg - 1; i >= 0; i--) { sp -= 4; M32(sp) = args[nreg + i]; }
    sp -= 4; M32(sp) = 0xc0dec0deu;            /* return address slot */
    R.esp = sp; R.ecx = nreg > 0 ? args[0] : 0; R.edx = nreg > 1 ? args[1] : 0;
    R.eax = 0; R.ebx = addr; R.esi = 0; R.edi = (u32)(uintptr_t)args; R.ebp = 0;
    R.top = 0; R.tag = 0; R.df = 0;
    rc_call(addr);
}''')
    for a, r in rows:
        if r['fret']:
            g.append('long double W_%s(u32 a0,u32 a1,u32 a2,u32 a3,u32 a4,u32 a5,u32 a6,u32 a7,u32 a8,u32 a9,u32 a10,u32 a11,u32 a12,u32 a13,u32 a14,u32 a15)' % a)
            g.append('{ u32 args[16]={a0,a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11,a12,a13,a14,a15}; rc_enter(0x%su, %d, %d, args); f80 v = ST(0); fpop(); return v; }' % (a, r['n'], r['nreg']))
        else:
            g.append('u32 W_%s(u32 a0,u32 a1,u32 a2,u32 a3,u32 a4,u32 a5,u32 a6,u32 a7,u32 a8,u32 a9,u32 a10,u32 a11,u32 a12,u32 a13,u32 a14,u32 a15)' % a)
            g.append('{ u32 args[16]={a0,a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11,a12,a13,a14,a15}; rc_enter(0x%su, %d, %d, args); return R.eax; }' % (a, r['n'], r['nreg']))
    g.append('struct diff_entry { u32 addr; void *fn; int nparams; int nreg; int fret; int rsize; };')
    g.append('const struct diff_entry diff_table[] = {')
    for a, r in rows:
        g.append('  {0x%su, (void *)W_%s, %d, %d, %d, %d},' % (a, a, r['n'], r['nreg'], r['fret'], r['rsize']))
    g.append('  {0, 0, 0, 0, 0, 0}};')
    g.append('struct pilot_fn_entry { u32 addr; void *fn; int translated; };')
    g.append('const struct pilot_fn_entry pilot_fn_table[] = {{0, 0, 0}};')
    open(bd + '/glue.c', 'w').write('\n'.join(g) + '\n')
    rt = [T + '/runtime/' + x for x in ['batch_main.c', 'pilot_rt.c', 'hybrid.c', 'call_orig.c', 'win32_stubs.c']] + [ROOT + '/rt/difftest_rc.c']
    fx = [fixture] if fixture != '-' else []
    link = ['gcc', '-m32', '-std=gnu11', '-O1', '-g', '-fno-strict-aliasing', '-fwrapv', '-w', '-fno-pie', '-no-pie',
            '-I' + T + '/src', '-I' + ROOT + '/rt'] + rt + fx + [ROOT + '/rt/rc_rt.c', ROOT + '/rt/bridge.S', bd + '/glue.c'] + objs + ['-lm', '-o', bd + '/test']
    subprocess.run(['nice'] + link, check=True)
    json.dump({'tests': [a for a, r in rows], 'status': {'%08x' % a: s for a, s in status.items()},
               'lifted': len(lifted), 'lift_sec': lift_sec, 'compile_sec': cc_sec, 'compiled': len(todo)},
              open(bd + '/lift.json', 'w'), indent=1)
    print('lifted %d functions in %.1fs, compiled %d in %.1fs, %d test rows' % (len(lifted), lift_sec, len(todo), cc_sec, len(rows)))

if __name__ == '__main__':
    main()

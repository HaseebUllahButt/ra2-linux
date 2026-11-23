#!/usr/bin/env python3
"""coverage.py: how much of game.exe .text the function-entry list reaches.
usage: coverage.py [entry-list] [-v] [--gaps FILE]
Runs lift.Func.discover() from every entry (8 processes), joins the instruction
bytes, and sorts every uncovered byte into: padding, jump table (data), other
data, dead code, unlisted code. Prints the totals and each non-padding gap.
--gaps FILE writes all gaps (tab separated) to FILE."""
import os, re, struct, sys
import multiprocessing
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import lift as L
from capstone import Cs, CS_ARCH_X86, CS_MODE_32
from capstone.x86 import X86_OP_IMM, X86_OP_MEM

PROCS = int(os.environ.get('RC_PROCS', '2'))   # owner rule: at most 2 workers

# instructions that never show up in this game's compiled code: decoding into them means data
ODD = {'in', 'out', 'insb', 'insd', 'outsb', 'outsd', 'arpl', 'les', 'lds', 'into', 'bound', 'aaa', 'aas', 'daa', 'das',
       'aam', 'aad', 'salc', 'hlt', 'cli', 'sti', 'iretd', 'retf', 'lcall', 'ljmp', 'int1', 'icebp', 'xlatb', 'sldt',
       'lsl', 'lar', 'verr', 'verw', 'clts', 'invd', 'wbinvd', 'rsm', 'pushal', 'popal', 'int', 'loopne', 'loope',
       'fbld', 'fbstp', 'lock', 'ud2'}
NOPS = {b'\x90', b'\x8b\xc0', b'\x8b\xff', b'\x8b\xc9', b'\x8b\xd2', b'\x8b\xdb', b'\x8b\xe4', b'\x8b\xed', b'\x8b\xf6',
        b'\x8d\x40\x00', b'\x8d\x49\x00', b'\x8d\x52\x00', b'\x8d\x5b\x00', b'\x8d\x64\x24\x00', b'\x8d\x6d\x00',
        b'\x8d\x76\x00', b'\x8d\x7f\x00', b'\x8d\x80\x00\x00\x00\x00', b'\x8d\x89\x00\x00\x00\x00',
        b'\x8d\x92\x00\x00\x00\x00', b'\x8d\x9b\x00\x00\x00\x00', b'\x8d\xa4\x24\x00\x00\x00\x00',
        b'\x8d\xad\x00\x00\x00\x00', b'\x8d\xb6\x00\x00\x00\x00', b'\x8d\xbf\x00\x00\x00\x00', b'\x87\xdb', b'\x87\xc9',
        b'\x87\xd2', b'\x87\xf6', b'\x87\xff', b'\x86\xc0', b'\x86\xc9'}

_G = {}

def _init(entries):
    _G['img'] = L.Image(L.EXE)
    md = Cs(CS_ARCH_X86, CS_MODE_32); md.detail = True
    _G['md'] = md
    _G['entries'] = entries

def discover_one(a, img=None, md=None, entries=None):
    """Discover one function. Returns a plain dict (picklable)."""
    img = img or _G['img']; md = md or _G['md']; entries = entries if entries is not None else _G['entries']
    f = L.Func(img, md, a, entries | {a})
    r = {'entry': a, 'ok': True}
    try:
        f.discover()
    except L.LiftError as ex:
        r['ok'] = False; r['error'] = str(ex)
    except Exception as ex:  # discovery should not crash; report it
        r['ok'] = False; r['error'] = 'crash: %r' % ex
    lo, hi = img.text_lo, img.text_hi
    r['insns'] = [(x, i.size) for x, i in f.insns.items()]
    r['bad'] = dict(f.bad)
    r['calls'] = sorted(f.calls)
    odd, refs, ptrs, tabs = [], set(), set(), []
    for x, i in f.insns.items():
        m = i.mnemonic
        if m in ODD or (m.startswith('rep') and m.split()[-1] in ODD):
            odd.append(x)
        if i.bytes[:2] == bytearray(b'\0\0'):
            odd.append(x)
        if i.bytes[0] in (0x06, 0x07, 0x0e, 0x16, 0x17, 0x1e, 0x1f):   # push/pop es, cs, ss, ds
            odd.append(x)
        branch = m == 'call' or m.startswith('j') or m.startswith('loop')
        for op in i.operands:
            if op.type == X86_OP_IMM and not branch:
                v = op.imm & 0xffffffff
                if lo <= v < hi: ptrs.add(v)
            elif op.type == X86_OP_MEM:
                v = op.mem.disp & 0xffffffff
                if lo <= v < hi:
                    refs.add((v, op.size, bool(op.mem.base or op.mem.index)))
    for ja, tl in f.tables.items():
        d = f.insns[ja].operands[0].mem.disp & 0xffffffff
        tabs.append((d, 4 * len(tl)))
    r['odd'] = odd
    try:
        r['flagbad'] = L.flag_check(f)[:1] if r['ok'] and f.insns and not (f.calls & getattr(L, 'FLAGRET', set())) else []
    except Exception as ex:
        r['flagbad'] = ['flag check crash: %r' % ex]
    r['refs'] = sorted(refs)
    r['ptrs'] = sorted(ptrs)
    r['tables'] = tabs
    r['jmp_targets'] = sorted(t for x, i in f.insns.items() if i.mnemonic == 'jmp' and i.operands[0].type == X86_OP_IMM
                              for t in [i.operands[0].imm & 0xffffffff] if t not in f.insns)
    r['labels'] = sorted(f.targets)
    # can this function return to its caller? (ret, or leaves by a jump we cannot follow)
    r['returns'] = bool(r['jmp_targets']) or any(
        i.mnemonic in ('ret', 'retf') or (i.mnemonic == 'jmp' and i.operands[0].type != X86_OP_IMM and x not in f.tables)
        for x, i in f.insns.items())
    r['callsites'] = sorted((x + i.size, i.operands[0].imm & 0xffffffff) for x, i in f.insns.items()
                            if i.mnemonic == 'call' and i.operands[0].type == X86_OP_IMM)
    return r

def discover_all(entries, addrs=None, procs=PROCS):
    addrs = sorted(entries if addrs is None else addrs)
    if not addrs:
        return {}
    with multiprocessing.get_context("fork").Pool(procs, _init, (frozenset(entries),)) as p:
        out = p.map(discover_one, addrs, chunksize=max(1, min(64, len(addrs) // (procs * 8) or 1)))
    return {r['entry']: r for r in out}

def load_entries(path):
    return set(int(x, 16) for l in open(path) for x in l.split("#")[0].split())

def data_pointers(img):
    """Aligned 32-bit values in .rdata/.data that point into .text: {target: [where...]}"""
    out = {}
    for name, a, vs, ra, rs in img.secs:
        if name not in ('.rdata', '.data'):
            continue
        b = img.d[ra:ra + min(vs, rs)]
        for o in range(0, len(b) - 3, 4):
            v = struct.unpack_from('<I', b, o)[0]
            if img.text_lo <= v < img.text_hi:
                out.setdefault(v, []).append(a + o)
    return out

class Cover:
    """Byte map of .text: 0 none, 1 instruction, 2 jump table, 3 referenced data."""
    def __init__(self, img, results):
        self.img = img
        lo, hi = img.text_lo, img.text_hi
        self.lo, self.hi = lo, hi
        n = hi - lo
        self.m = bytearray(n)
        self.start = bytearray(n)   # 1 where some instruction starts
        self.owner = {}
        for r in results.values():
            for x, s in r['insns']:
                if lo <= x < hi:
                    self.m[x - lo:x - lo + s] = b'\1' * min(s, hi - x)
                    self.start[x - lo] = 1
                    self.owner.setdefault(x, r['entry'])
        self.tables = []
        self.refs = {}
        for r in results.values():
            for d, s in r['tables']:
                self.tables.append((d, s))
            for v, sz, idx in r['refs']:
                self.refs.setdefault(v, []).append(r['entry'])
        for d, s in self.tables:
            for k in range(max(d, lo), min(d + s, hi)):
                if not self.m[k - lo]:
                    self.m[k - lo] = 2
        self.calls = {}
        self.ptrs = {}
        for r in results.values():
            for c in r['calls']:
                self.calls.setdefault(c, []).append(r['entry'])
            for p in r['ptrs']:
                self.ptrs.setdefault(p, []).append(r['entry'])
            for t in r['jmp_targets']:
                self.calls.setdefault(t, []).append(r['entry'])

    def gaps(self):
        """Maximal runs of uncovered bytes: list of (start, end)."""
        return [(self.lo + g.start(), self.lo + g.end()) for g in re.finditer(b'\x00+', bytes(self.m))]

def nop_len(b):
    for k in (7, 6, 4, 3, 2, 1):
        if len(b) >= k and bytes(b[:k]) in NOPS:
            return k
    return 0

def split_padding(img, s, e):
    """Returns (pad_bytes, cores): padding (int3, nop forms, zero runs) cut from the edges of the gap
    and from inside it (int3 runs of 2 or more split it)."""
    b = img.read(s, e - s)
    pieces, i, cur = [], 0, 0
    while i < len(b):
        if b[i] == 0xcc and i + 1 < len(b) and b[i + 1] == 0xcc:
            j = i
            while j < len(b) and b[j] == 0xcc:
                j += 1
            pieces.append((cur, i)); cur = j; i = j
        else:
            i += 1
    pieces.append((cur, len(b)))
    pad, cores = 0, []
    for ps, pe in pieces:
        a, z = ps, pe
        while a < z:
            if b[a] in (0xcc, 0):
                a += 1; continue
            k = nop_len(b[a:min(z, a + 8)])
            if not k:
                break
            a += k
        while z > a and b[z - 1] in (0xcc, 0x90, 0):
            z -= 1
        pad += (pe - ps) - (z - a)
        if z > a:
            cores.append((s + a, s + z))
    return pad, cores

def classify(img, md, cov, s, e, dptr, entries):
    """Class of a non-padding gap [s, e)."""
    lo, hi = img.text_lo, img.text_hi
    b = img.read(s, e - s)
    # dword table: every aligned dword points into .text near here
    if e - s >= 8 and (e - s) % 4 == 0:
        vals = [struct.unpack_from('<I', b, k)[0] for k in range(0, e - s, 4)]
        if all(lo <= v < hi and abs(v - s) < 0x20000 for v in vals):
            return 'jump table'
    # byte index table: referenced by a movzx from code
    if s in cov.refs or any(v in cov.refs for v in range(s - 3, s)):
        if all(x < 0x80 for x in b) or s in cov.refs:
            return 'jump table' if all(x < 0x80 for x in b) else 'other data'
    # mostly small byte values: a table of bytes, not code
    if sum(1 for x in b if x < 0x10) * 2 >= len(b):
        return 'other data'
    refd = s in dptr or s in cov.calls or s in cov.ptrs
    # code check: decode linearly to the end of the gap without odd instructions
    a = s
    okcode = True
    while a < e:
        ins = next(md.disasm(img.read(a, 16), a, 1), None)
        if ins is None or ins.mnemonic in ODD or ins.bytes[:2] == bytearray(b'\0\0'):
            okcode = False; break
        a += ins.size
    if okcode and a == e:
        return 'unlisted code' if refd else 'dead code'
    return 'other data'

def report(entries_path, verbose=False, gaps_file=None):
    entries = load_entries(entries_path)
    img = L.Image(L.EXE)
    md = Cs(CS_ARCH_X86, CS_MODE_32); md.detail = True
    res = discover_all(entries)
    cov = Cover(img, res)
    dptr = data_pointers(img)
    total = cov.hi - cov.lo
    covered = sum(1 for x in cov.m if x == 1)
    tabbytes = sum(1 for x in cov.m if x == 2)
    pad = 0
    klass = {'jump table': tabbytes, 'other data': 0, 'dead code': 0, 'unlisted code': 0}
    rows = []
    for s, e in cov.gaps():
        p, cores = split_padding(img, s, e)
        pad += p
        for cs, ce in cores:
            k = classify(img, md, cov, cs, ce, dptr, entries)
            klass[k] += ce - cs
            rows.append((cs, ce, k))
    data = klass['jump table'] + klass['other data']
    nonpad = total - pad
    code = nonpad - data
    fails = sum(1 for r in res.values() if not r['ok'] or any(v == 'cannot decode' for v in r['bad'].values()))
    print('entry list          %s (%d entries, %d with decode problems)' % (entries_path, len(entries), fails))
    print('.text bytes         %d' % total)
    print('padding bytes       %d' % pad)
    print('non-padding bytes   %d' % nonpad)
    print('covered bytes       %d' % covered)
    print('gap classes (bytes, gaps):')
    for k in ('jump table', 'other data', 'dead code', 'unlisted code'):
        print('  %-17s %8d  %5d' % (k, klass[k], sum(1 for r in rows if r[2] == k) + (len(cov.tables) if k == 'jump table' else 0)))
    print('covered / non-padding bytes         %.3f%%' % (100.0 * covered / nonpad))
    print('covered / non-padding code bytes    %.3f%%  (code = non-padding minus jump tables and other data)' % (100.0 * covered / code))
    print('non-padding gaps (start end size class):')
    shown = 0
    for cs, ce, k in rows:
        if verbose or k in ('unlisted code',) or shown < 40:
            print('  %08x %08x %6d %s' % (cs, ce, ce - cs, k))
            shown += 1
    if not verbose and shown < len(rows):
        print('  ... %d more (use -v)' % (len(rows) - shown))
    if gaps_file:
        with open(gaps_file, 'w') as f:
            for cs, ce, k in rows:
                f.write('%08x\t%08x\t%d\t%s\n' % (cs, ce, ce - cs, k))
    return res, cov, rows

if __name__ == '__main__':
    args = [a for a in sys.argv[1:] if not a.startswith('-')]
    gf = None
    if '--gaps' in sys.argv:
        gf = sys.argv[sys.argv.index('--gaps') + 1]
        args.remove(gf)
    report(args[0] if args else L.ENTRIES, '-v' in sys.argv, gf)

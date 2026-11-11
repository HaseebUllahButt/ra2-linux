#!/usr/bin/env python3
"""insnfuzz.py: instruction fuzzer for tools/lift.py.
  insnfuzz.py collect <forms.json>          every instruction form in game.exe, with real examples
  insnfuzz.py gen <forms.json> <out-dir>    lift each example with the real Emitter, write fuzz C
The C harness (tests/fuzz/harness.c, native.S) runs the real bytes and the lifted C on random
states and compares them. tools/insnfuzz.sh does all steps."""
import json, os, random, re, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import lift as L
from capstone import Cs, CS_ARCH_X86, CS_MODE_32
from capstone.x86 import X86_OP_REG, X86_OP_IMM, X86_OP_MEM

PREFIX_NAMES = {0xf0: 'lock', 0xf2: 'repne', 0xf3: 'rep', 0x66: 'o16', 0x67: 'a16',
                0x2e: 'cs', 0x36: 'ss', 0x3e: 'ds', 0x26: 'es', 0x64: 'fs', 0x65: 'gs'}
REGIDX = {r: i for i, r in enumerate(L.REG32)}

def prefixes(ins):
    out = []
    for b in ins.bytes:
        if b in PREFIX_NAMES: out.append(PREFIX_NAMES[b])
        else: break
    return out

def regclass(name):
    if name in ('esp', 'sp'): return 'rsp'
    if name in L.REG32: return 'r32'
    if name in ('al', 'cl', 'dl', 'bl'): return 'r8l'
    if name in ('ah', 'ch', 'dh', 'bh'): return 'r8h'
    if name in ('ax', 'cx', 'dx', 'bx', 'bp', 'si', 'di'): return 'r16'
    if name == 'st(0)': return 'st0'
    if name.startswith('st('): return 'sti'
    return name

def form_key(ins):
    """mnemonic + prefixes + operand kinds and sizes (+ register aliasing)"""
    parts = [ins.mnemonic] + prefixes(ins)
    regs = []; memregs = set()
    for op in ins.operands:
        if op.type == X86_OP_REG:
            n = ins.reg_name(op.reg)
            parts.append(regclass(n)); regs.append(L.reg32(n))
        elif op.type == X86_OP_IMM:
            parts.append('i%d' % op.size)
        else:
            m = op.mem
            shape = ('b' if m.base else '') + ('x%d' % m.scale if m.index else '') + ('d' if m.disp or not (m.base or m.index) else '')
            if m.base and ins.reg_name(m.base) == 'esp': shape = 'S' + shape
            seg = ins.reg_name(m.segment) + ':' if m.segment else ''
            parts.append('%sm%d.%s' % (seg, op.size, shape))
            if m.base: memregs.add(L.reg32(ins.reg_name(m.base)))
            if m.index: memregs.add(L.reg32(ins.reg_name(m.index)))
            if m.base and m.index and m.base == m.index: parts.append('bx-same')
    if len(regs) != len(set(regs)): parts.append('same')
    if memregs & set(regs): parts.append('alias')
    return ' '.join(parts)

def collect(out):
    img = L.Image(L.EXE)
    md = Cs(CS_ARCH_X86, CS_MODE_32); md.detail = True
    entries = set(int(x, 16) for x in open(L.ENTRIES).read().split())
    forms = {}
    nfun = nins = 0; failed = []
    for e in sorted(entries):
        f = L.Func(img, md, e, entries | {e})
        try:
            f.discover()
        except L.LiftError as ex:
            failed.append('%08x %s' % (e, ex)); continue
        nfun += 1
        for a in sorted(f.insns):
            ins = f.insns[a]; nins += 1
            k = form_key(ins)
            fm = forms.setdefault(k, {'count': 0, 'examples': []})
            fm['count'] += 1
            ex = fm['examples']
            b = bytes(ins.bytes).hex()
            if len(ex) < 3 and all(x['bytes'] != b for x in ex):
                ex.append({'addr': '%08x' % a, 'bytes': b, 'text': '%s %s' % (ins.mnemonic, ins.op_str), 'func': '%08x' % e})
    json.dump({'functions': nfun, 'instructions': nins, 'discover_failed': failed,
               'forms': dict(sorted(forms.items()))}, open(out, 'w'), indent=1)
    print('%d functions, %d instructions, %d forms, %d functions failed discovery' % (nfun, nins, len(forms), len(failed)))

SKIP_MN = {'int3': 'trap', 'int': 'trap', 'into': 'trap', 'hlt': 'privileged', 'ud2': 'trap', 'cpuid': 'machine query (J16)',
           'rdtsc': 'time counter (J16)', 'in': 'privileged', 'out': 'privileged', 'cli': 'privileged', 'sti': 'privileged'}
STRING = ('movs', 'stos', 'lods', 'scas', 'cmps')
F_C, F_P, F_Z, F_S, F_O = 0x1, 0x4, 0x40, 0x80, 0x800
X87_CMP = ('fcom', 'fcomp', 'fcompp', 'fucom', 'fucomp', 'fucompp', 'ficom', 'ficomp', 'ftst', 'fxam', 'fprem', 'fprem1', 'fninit')
X87_C2 = ('fsin', 'fcos', 'fsincos', 'fptan')

class FakeF:
    """the parts of lift.Func that Emitter reads"""
    def __init__(self, insns):
        self.insns, self.bad, self.tables, self.entry = insns, {}, {}, 0

def x87_mfmt(b):
    i = 0
    while b[i] in (0x66, 0x67, 0xf2, 0xf3, 0x2e, 0x36, 0x3e, 0x26, 0x64, 0x65, 0x9b): i += 1
    op, reg = b[i], (b[i + 1] >> 3) & 7
    if op == 0xd8: return 'MF_F32'
    if op == 0xdc: return 'MF_F64'
    if op == 0xda: return 'MF_I32'
    if op == 0xde: return 'MF_I16'
    if op == 0xd9: return {0: 'MF_F32', 5: 'MF_CW'}.get(reg, 'MF_INT')
    if op == 0xdb: return {0: 'MF_I32', 5: 'MF_F80'}.get(reg, 'MF_INT')
    if op == 0xdd: return {0: 'MF_F64'}.get(reg, 'MF_INT')
    if op == 0xdf: return {0: 'MF_I16', 5: 'MF_I64'}.get(reg, 'MF_INT')
    return 'MF_INT'

def cstr(s):
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"') + '"'

def gen_example(i, key, ex, md):
    """returns (c_code, table_row) or (None, skip_reason)"""
    b = bytes.fromhex(ex['bytes']); a = int(ex['addr'], 16)
    ins = next(md.disasm(b, a, 1))
    m = ins.mnemonic.split()[-1]
    ops = ins.operands
    nxt = a + ins.size
    if m in SKIP_MN:
        return None, SKIP_MN[m]
    if m == 'emms' or any(o.type == X86_OP_REG and ins.reg_name(o.reg).startswith('mm') for o in ops):
        return None, 'MMX (helpers checked by tests/mmx against the real MMX unit)'
    if any(o.type == X86_OP_MEM and o.mem.segment and ins.reg_name(o.mem.segment) in ('fs', 'gs') for o in ops):
        return None, 'fs: access (thread block, needs Windows)'
    x87 = 0xd8 <= ins.opcode[0] <= 0xdf
    kind = []; target = 0; indreg = -1; btreg = -1; dsize = 0
    if x87: kind.append('K_X87')
    insns = {a: ins}
    branch = m.startswith('j') or m.startswith('loop') or m == 'call'
    if branch and ops and ops[0].type == X86_OP_IMM:
        target = ops[0].imm & 0xffffffff
        if a - 6 < target < nxt + 6:
            return None, 'branch target overlaps the instruction (cannot place stubs)'
        kind.append('K_BR')
        if m != 'call': insns[target] = None
    elif m in ('jmp', 'call'):
        kind.append('K_IND')
        if ops[0].type == X86_OP_REG: indreg = REGIDX[L.reg32(ins.reg_name(ops[0].reg))]
    if m == 'ret': kind.append('K_RET')
    if m in ('popfd', 'popf'): kind.append('K_POPF')
    if m == 'leave': kind.append('K_LEAVE')
    str_op = m[:-1] in STRING and m[-1] in 'bwd' and not x87
    if str_op:
        kind.append('K_STRING')
        if ins.prefix[0] in (0xf2, 0xf3): kind.append('K_REP')
    if m in ('div', 'idiv'):
        kind.append('K_DIV'); dsize = ops[0].size
        if m == 'idiv': kind.append('K_IDIV')
    if m in ('bt', 'bts', 'btr', 'btc') and ops[0].type == X86_OP_MEM and ops[1].type == X86_OP_REG:
        kind.append('K_BTMEM'); btreg = REGIDX[L.reg32(ins.reg_name(ops[1].reg))]
    em = L.Emitter(FakeF(insns))
    em.out = []
    try:
        em.emit(ins)
    except L.LiftError as ex_:
        return None, 'lifter rejects: %s' % ex_
    body = em.out
    # memory operand: the harness chooses registers so that it points into test memory
    mem = [o for o in ops if o.type == X86_OP_MEM]
    mrow = '0, -1, -1, 0, 0u, 0, MF_INT'
    if mem and not str_op and m not in ('lea', 'nop'):
        mo = mem[0].mem
        base = REGIDX[L.reg32(ins.reg_name(mo.base))] if mo.base else -1
        index = REGIDX[L.reg32(ins.reg_name(mo.index))] if mo.index else -1
        mf = x87_mfmt(b) if x87 else 'MF_INT'
        if x87 and mf == 'MF_CW': kind.append('K_FLDCW')
        mrow = '1, %d, %d, %d, 0x%xu, %d, %s' % (base, index, mo.scale if mo.index else 0, mo.disp & 0xffffffff, mem[0].size, mf)
    # lifted function: C locals from R and FZF, the emitted lines, then back
    c = ['void fzl_%d(void)' % i, '{',
         '    u32 eax=R.eax,ecx=R.ecx,edx=R.edx,ebx=R.ebx,esp=R.esp,ebp=R.ebp,esi=R.esi,edi=R.edi;',
         '    u8 cf=FZF.cf,zf=FZF.zf,sf=FZF.sf,of=FZF.of,pf=FZF.pf;',
         '    /* %08x %s %s */' % (a, ins.mnemonic, ins.op_str)]
    c += [l.replace('return;', '{ if (!FZ_exit) FZ_exit = 2; goto fz_out; }') for l in body]
    c.append('    goto fz_out;')
    if target in insns and target != a:
        c.append('L_%08x: FZ_exit = 1; goto fz_out;' % target)
    c += ['fz_out:', '    SAVE; FZF.cf=cf; FZF.zf=zf; FZF.sf=sf; FZF.of=of; FZF.pf=pf;', '}']
    # mask function: which results the Intel manual defines for this input
    k = ['void fzm_%d(void)' % i, '{',
         '    u32 eax=FZI[0],ecx=FZI[1],edx=FZI[2],ebx=FZI[3],esp=FZI[4],ebp=FZI[5],esi=FZI[6],edi=FZI[7];',
         '    (void)eax;(void)ecx;(void)edx;(void)ebx;(void)esp;(void)ebp;(void)esi;(void)edi;']
    def cnt(op):
        return '%du' % (op.imm & 31) if op.type == X86_OP_IMM else '(ecx & 31)'
    if m in ('mul', 'imul'):
        k.append('    FZ_fmask &= ~0x%xu;' % (F_Z | F_S | F_P))
    elif m in ('div', 'idiv'):
        k.append('    FZ_fmask = 0x400;')
    elif m in ('bt', 'bts', 'btr', 'btc'):
        k.append('    FZ_fmask &= ~0x%xu;' % (F_S | F_O | F_P))
    elif m in ('bsf', 'bsr'):
        em.ins = ins
        src = em.rd(ops[1])
        r = REGIDX[L.reg32(ins.reg_name(ops[0].reg))]
        k.append('    FZ_fmask &= ~0x%xu;' % (F_C | F_O | F_S | F_P))
        k.append('    if (((%s) & %s) == 0) FZ_rmask[%d] &= ~%s;' % (src, L.MASK[ops[0].size], r, L.MASK[ops[0].size]))
    elif m in ('shl', 'sal', 'shr', 'sar', 'rol', 'ror', 'rcl', 'rcr'):
        c_ = '1u' if len(ops) == 1 else cnt(ops[1])
        k.append('    u32 c = %s;' % c_)
        k.append('    if (c > 1) FZ_fmask &= ~0x%xu;' % F_O)
        if m in ('shl', 'sal', 'shr') and ops[0].size < 4:
            k.append('    if (c >= %d) FZ_fmask &= ~0x%xu;' % (L.BITS[ops[0].size], F_C))
    elif m in ('shld', 'shrd'):
        k.append('    u32 c = %s;' % cnt(ops[2]))
        k.append('    if (c > 1) FZ_fmask &= ~0x%xu;' % F_O)
    elif m == 'lahf':
        k.append('    FZ_rmask[0] &= ~0x1000u;   /* AF is not modelled */')
    elif m in ('pushfd', 'pushf'):
        k.append('    FZ_mm_addr = esp - 4; FZ_mm_mask = 0xec7u;   /* CF PF ZF SF IF DF OF and bit 1; AF is not modelled */')
    if x87:
        if m in X87_CMP: k.append('    FZ_cmask = 0xf;')
        elif m in X87_C2: k.append('    FZ_cmask = 0x4;')
    k.append('}')
    hexb = '"' + ''.join('\\x%02x' % x for x in b[:ins.size]) + '"'
    row = '{%s, %s, 0x%08xu, %d, %s, fzl_%d, fzm_%d, %s, %s, 0x%08xu, %d, %d, %d}' % (
        cstr(key), cstr(ex['text']), a, ins.size, hexb, i, i, '|'.join(kind) or '0', mrow, target, indreg, btreg, dsize)
    return '\n'.join(c + k), row

def gen(forms_path, out):
    d = json.load(open(forms_path))
    md = Cs(CS_ARCH_X86, CS_MODE_32); md.detail = True
    os.makedirs(out, exist_ok=True)
    open(os.path.join(out, '.trackerignore'), 'w').close()
    rows, codes, skips, keys = [], [], {}, set()
    i = 0
    for key, fm in d['forms'].items():
        for ex in fm['examples']:
            code, row = gen_example(i, key, ex, md)
            if code is None:
                skips.setdefault(key, (row, fm['count'], ex['text']))
                continue
            codes.append(code); rows.append(row); keys.add(key); i += 1
    nchunk = 4
    for c in range(nchunk):
        open('%s/gen%d.c' % (out, c), 'w').write('#include "fz.h"\n' + '\n'.join(codes[c::nchunk]) + '\n')
    decl = ['#include "fz.h"']
    for j in range(i):
        decl.append('void fzl_%d(void); void fzm_%d(void);' % (j, j))
    decl.append('const struct fzex fz_table[] = {')
    decl += ['  %s,' % r for r in rows]
    decl.append('};')
    decl.append('const int fz_n = %d;' % len(rows))
    open('%s/table.c' % out, 'w').write('\n'.join(decl) + '\n')
    # a form is skipped only when none of its examples can run
    sk = {k: v for k, v in skips.items() if k not in keys}
    with open('%s/skipped.txt' % out, 'w') as f:
        for k, (why, n, text) in sorted(sk.items()):
            f.write('%s | %d uses | %s | %s\n' % (k, n, text, why))
    print('%d forms, %d forms tested (%d examples), %d forms skipped' % (len(d['forms']), len(keys), len(rows), len(sk)))

if __name__ == '__main__':
    if sys.argv[1] == 'collect':
        collect(sys.argv[2])
    elif sys.argv[1] == 'gen':
        gen(sys.argv[2], sys.argv[3])

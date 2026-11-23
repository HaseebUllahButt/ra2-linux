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
        k.append('    FZ_mm_addr[0] = esp - 4; FZ_mm_mask[0] = 0x240ec7u;   /* CF PF ZF SF IF DF OF AC ID and bit 1; AF is not modelled */')
    if x87:
        if m in X87_CMP: k.append('    FZ_cmask = 0xf;')
        elif m in X87_C2: k.append('    FZ_cmask = 0x4;')
    k.append('}')
    hexb = '"' + ''.join('\\x%02x' % x for x in b[:ins.size]) + '"'
    row = '{%s, %s, 0x%08xu, %d, %s, fzl_%d, fzm_%d, %s, %s, 0x%08xu, %d, %d, %d, 0u}' % (
        cstr(key), cstr(ex['text']), a, ins.size, hexb, i, i, '|'.join(kind) or '0', mrow, target, indreg, btreg, dsize)
    return '\n'.join(c + k), row

# ---- extra checks (J5b): the lifter bug classes in pcrecomp's CHANGELOG ----
# Single forms that game.exe does not use but the lifter accepts (run at made-up addresses).
EXTRA_BASE = 0x40000000   # far from the heap and from the game image addresses
EXTRA = [
    # 8/16-bit one-operand mul/imul/div/idiv use AX and DX:AX (pcrecomp #6)
    'f6e1', 'f6e9', 'f6f1', 'f6f9', '66f7e1', '66f7e9', '66f7f3', '66f7fb',
    # adc/sbb at 8 and 16 bits; neg 16
    '10c8', '18e0', '6611c8', '6619c8', '66f7d8',
    # rotates through carry with a count in cl, narrow sar (pcrecomp #72, #88)
    'd2d0', 'd2d8', '66d3d0', '66d3d8', 'd3d0', 'd3d8', 'd1da', 'd2f8', '66d3f8', 'd2c0', '66d3c8',
    # shrd (not in game), shld by cl, shld with count 0
    '0fadc2', '0facc205', '0fa5c2', '0fa4c200',
    # bsf/bsr, bt/bts/btr/btc reg,reg, bswap, imul 16-bit 3 operands
    '0fbcc1', '0fbdc1', '660fbcc1', '0fa3c8', '0fabc8', '0fb3c8', '0fbbc8', '0fc8', '666bc1fd',
    # bt/btr/btc on a memory bit string with a register offset, 32 and 16-bit (signed offset; pcrecomp #89)
    '0fa306', '0fb306', '0fbb06', '660fa306', '660fab06', '660fb306',
    # xadd / cmpxchg (lock forms), lahf (AF masked: not modelled)
    'f00fc10e', '0fc0c8', 'f00fb10e', '0fb1ca', '9f',
    # loop / jcxz / every short jcc and setcc (jo jno jp jnp seto setno setp setnp are not in game.exe)
    'e210', '67e310',
    '7010', '7110', '7210', '7310', '7410', '7510', '7610', '7710', '7810', '7910', '7a10', '7b10', '7c10', '7d10', '7e10', '7f10',
    '0f90c0', '0f91c0', '0f92c0', '0f93c0', '0f94c0', '0f95c0', '0f96c0', '0f97c0',
    '0f98c0', '0f99c0', '0f9ac0', '0f9bc0', '0f9cc0', '0f9dc0', '0f9ec0', '0f9fc0',
    # string ops without rep, enter
    'a4', 'a5', 'aa', 'ab', 'ac', 'ad', 'ae', 'af', 'a6', 'a7', 'c8080000',
    # x87 forms pcrecomp got wrong and game.exe does not use: fsincos order, fsin/fcos/fptan C2,
    # fprem/fprem1 C bits, fxam, fucom*, fst st(i), fld/fstp m80, fild/fistp m64, fist m16/m32, ftst, fpatan...
    'd9fb', 'd9fe', 'd9ff', 'd9f2', 'd9f8', 'd9f5', 'd9e5', 'dae9', 'dde1', 'dde9', 'ddd1',
    'db2c24', 'db3c24', 'df2c24', 'df3c24', 'df1424', 'db1424', 'db1c24', 'd9e4', 'd9f3', 'd9f1', 'd9fd', 'd9f0',
]

# Multi-instruction sequences: flags, carry and x87 state that live across instructions.
# Real code from game.exe: 'addr' and 'end'. Made-up code: 'hex' (placed at a made-up address).
# 'mask': extra C for the mask function (input registers as eax.., FZ_* masks); 'kind': extra harness kinds.
AFMASK2 = ('FZ_mm_addr[0] = esp - 4; FZ_mm_mask[0] = ~0x10u; FZ_mm_addr[1] = esp - 8; FZ_mm_mask[1] = ~0x10u; '
           'FZ_rmask[0] &= ~0x10u; FZ_rmask[1] &= ~0x10u; FZ_rmask[3] &= ~0x10u;')
CMP = 'FZ_cmask = 0xf;'
SH = 'if ((ecx & 31) > 1) FZ_fmask &= ~0x800u;'
SEQS = [
    # carry chains: CF written by one instruction, read by the next (pcrecomp sbb idiom, precise_carry, add -> adc)
    dict(cls='carry', name='cmp; sbb same', hex='39c819c0'),
    dict(cls='carry', name='add; adc', hex='01c811da'),
    dict(cls='carry', name='sub; sbb', hex='29c819da'),
    dict(cls='carry', name='neg; sbb same', hex='f7d819c0'),
    dict(cls='carry', name='game neg dl; sbb edx,edx', addr=0x0040165e, end=0x00401662),
    dict(cls='carry', name='game sbb; sbb -1', addr=0x00422215, end=0x0042221a),
    dict(cls='carry', name='game cmp; sbb', addr=0x00430429, end=0x0043042d),
    dict(cls='carry', name='game add; adc', addr=0x00405081, end=0x00405085),
    dict(cls='carry', name='game sub; sbb', addr=0x00404fec, end=0x00404ff0),
    dict(cls='carry', name='game neg; adc 0', addr=0x00786a88, end=0x00786a8d),
    dict(cls='carry', name='game neg; sbb 0', addr=0x007824e4, end=0x007824e9),
    dict(cls='carry', name='game shr; rep stosd; adc; rep stosw', addr=0x0060092f, end=0x00600938),
    dict(cls='carry', name='game shr; rep movsw; adc; rep movsb', addr=0x0077ee43, end=0x0077ee4c),
    dict(cls='carry', name='game shr/rcr 64-bit loop', addr=0x00782537, end=0x00782543),
    dict(cls='carry', name='game shld chain', addr=0x0077efdc, end=0x0077f01a),
    # flags kept by inc/dec (pcrecomp: inc/dec keep CF)
    dict(cls='keep', name='stc; inc; adc', hex='f94083d300'),
    dict(cls='keep', name='cmp; dec; setb', hex='39c84a0f92c3'),
    # pushfd/popfd round trips (pcrecomp recomp_eflags): CF, DF, and the AC/ID bits of CPU checks
    dict(cls='pushf', name='stc; pushfd; clc; popfd; adc', hex='f99cf89d83d300'),
    dict(cls='pushf', name='std; pushfd; cld; popfd', hex='fd9cfc9d'),
    dict(cls='pushf', name='game saturating add (pushfd/popfd around cmp)', addr=0x0077ee6b, end=0x0077ee87,
         mask='FZ_mm_addr[0] = esp - 4; FZ_mm_mask[0] = ~0x10u;'),
    dict(cls='pushf', name='game CPU check: AC and ID flags toggle (0077f282)', addr=0x0077f289, end=0x0077f2c5, mask=AFMASK2),
    dict(cls='pushf', name='game CPU check: AC and ID flags toggle (0077f14d)', addr=0x0077f161, end=0x0077f18b, mask=AFMASK2),
    # logic ops then signed/unsigned conditions (pcrecomp: jcc after or/xor), bt then setae
    dict(cls='logic', name='xor/or/test then setge/setle/setg', hex='31c80f9dc209db0f9ec685fe0f9fc0'),
    dict(cls='logic', name='bt; setae', hex='0fa3c80f93c2', mask='FZ_fmask &= ~0x884u;'),
    # x87 compare -> fnstsw -> sahf/test -> jcc (pcrecomp: fcom flags, NaN = unordered, jp, fucompp pops)
    dict(cls='x87cmp', name='fcom; fnstsw; sahf; setp/seta/sete', hex='d8d1dfe09e0f9ac10f97c50f94c2', mask=CMP),
    dict(cls='x87cmp', name='fucompp; fnstsw; sahf; setp/setb', hex='dae9dfe09e0f9ac10f92c5', mask=CMP),
    dict(cls='x87cmp', name='ftst; fnstsw; test ah 41/05; sete/setp', hex='d9e4dfe0f6c4410f94c1f6c4050f9ac5', mask=CMP),
    dict(cls='x87cmp', name='fxam; fnstsw; and eax 4500', hex='d9e5dfe02500450000', mask=CMP),
    dict(cls='x87cmp', name='game fcomp m64; fnstsw; test ah; je', addr=0x00414c61, end=0x00414c72),
    dict(cls='x87cmp', name='game fcompp; fnstsw; test ah; jne', addr=0x00411df3, end=0x00411dfc, mask=CMP),
    dict(cls='x87cmp', name='game fcomp st1; fnstsw; sahf; jne', addr=0x007823a9, end=0x007823b3, mask=CMP),
    dict(cls='x87cmp', name='fcomp m64; fnstsw; sahf; setbe/seta/setp', hex='dc1c24dfe09e0f96c10f97c50f9ac2', mask=CMP),
    dict(cls='x87cmp', name='game fcomp st1; fnstsw; test; fstp; je', addr=0x00437280, end=0x0043728b),
    dict(cls='x87cmp', name='game fcomp m64abs; fnstsw; sahf; jae', addr=0x0078782a, end=0x00787835, mask=CMP),
    dict(cls='x87cmp', name='game fcomp; fnstsw; sahf; ja', addr=0x007877e3, end=0x007877ee, mask=CMP),
    dict(cls='x87cmp', name='game fcomp m32; fnstsw; mov al; test; jne', addr=0x004372f9, end=0x00437308),
    dict(cls='x87cmp', name='game fcomp st1; fnstsw; test; fstp; jne', addr=0x0045fa48, end=0x0045fa57),
    dict(cls='x87cmp', name='game fcomp; cmp; fnstsw; je', addr=0x006d6393, end=0x006d639f),
    dict(cls='x87cmp', name='game fcomp; fnstsw; test; mov; jne', addr=0x00437669, end=0x0043767e),
    dict(cls='x87cmp', name='game fcomp; fnstsw; sahf; jb', addr=0x007877f1, end=0x007877fc, mask=CMP),
    dict(cls='x87cmp', name='game fcomp; fnstsw; fstp; test; fstp; je', addr=0x0057d348, end=0x0057d359),
    dict(cls='x87cmp', name='game fcompp; fnstsw; fcom; test; fnstsw; je', addr=0x00721e0e, end=0x00721e1b),
    dict(cls='x87cmp', name='game fcomp; fnstsw; sahf; mov; jae', addr=0x0078776a, end=0x0078777b, mask=CMP),
    dict(cls='x87cmp', name='game fcomp; fnstsw; sahf; je', addr=0x0078d2ba, end=0x0078d2c9, mask=CMP),
    # x87 stack bookkeeping: no leaked or lost slots (pcrecomp: fucompp leak, fst st(i))
    dict(cls='x87stack', name='fld m32; fld st0; fmulp; fstp m64', hex='d90424d9c0dec9dd5c2408'),
    dict(cls='x87stack', name='fld1; fldz; fucompp; fnstsw', hex='d9e8d9eedae9dfe0', mask=CMP),
    dict(cls='x87stack', name='fld st0; fst st2; fstp st1', hex='d9c0ddd2ddd9'),
    dict(cls='x87stack', name='fild m64; fistp m64', hex='df2c24df7c2408'),
    dict(cls='x87stack', name='fld m80; fstp m80', hex='db2c24db7c2410'),
    # rep with ecx = 0 keeps flags (pcrecomp #8), direction flag, overlapping copies
    dict(cls='rep', name='cmp; repe cmpsb; sbb', hex='39d0f3a619c0'),
    dict(cls='rep', name='cmp; repne scasb; sete/setb', hex='39d0f2ae0f94c00f92c4'),
    dict(cls='rep', name='game std; rep movsd; cld', addr=0x00783443, end=0x00783447),
    dict(cls='rep', name='game std; repne scasb; inc; cmp', addr=0x00782005, end=0x0078200b),
    dict(cls='rep', name='std; rep stosd; cld', hex='fdf3abfc'),
    dict(cls='rep', name='rep movsb overlap', hex='f3a4', kind=['K_OVL']),
    dict(cls='rep', name='rep movsd overlap', hex='f3a5', kind=['K_OVL']),
    dict(cls='rep', name='std; rep movsb overlap; cld', hex='fdf3a4fc', kind=['K_OVL']),
    dict(cls='rep', name='std; rep movsd overlap; cld', hex='fdf3a5fc', kind=['K_OVL']),
    # count 0 keeps flags; counts at or above the width (pcrecomp: shift.by-zero, count-masked, shld/shrd)
    dict(cls='shift', name='cmp; shl cl; setb/sete', hex='39d0d3e30f92c00f94c4', mask=SH),
    dict(cls='shift', name='cmp; shld cl; setb/sete', hex='39d00fa5c30f92c00f94c4', mask=SH),
    dict(cls='shift', name='cmp; shrd cl; setb/sete', hex='39d00fadc30f92c00f94c4', mask=SH),
    dict(cls='shift', name='cmp; shr dx cl; sete', hex='39d066d3ea0f94c0', mask=SH + ' if ((ecx & 31) >= 16) FZ_fmask &= ~1u;'),
    dict(cls='shift', name='cmp; sar al cl; setl', hex='39d0d2f80f9cc4', mask=SH),
    # 8/16-bit partial registers
    dict(cls='partial', name='ah/al mix; xchg; movsx; sar cx', hex='88c400e080d40086e00fbfc866d1f9'),
    dict(cls='partial', name='add ax; adc dx; sbb si', hex='6601d86611ca6619fe'),
    # 3-operand imul overflow
    dict(cls='imul', name='imul r,r,imm32; seto; setc', hex='69c1452301000f90c20f92c6', mask='FZ_fmask &= ~0xc4u;'),
    dict(cls='imul', name='imul r16,r16,imm8; seto', hex='666bcafd0f90c0', mask='FZ_fmask &= ~0xc4u;'),
    # divides: the fault must happen on both sides; quotient overflow
    dict(cls='div', name='cdq; idiv', hex='99f7f9', mask='FZ_fmask = 0x400;'),
    dict(cls='div', name='xor edx; div', hex='31d2f7f1', mask='FZ_fmask = 0x400;'),
]
SEQ_BASE = 0x41000000

def gen_seq(i, k_, sq, md, img):
    """several instructions: returns (c_code, table_row) or (None, skip_reason)"""
    if 'hex' in sq:
        a = SEQ_BASE + 0x1000 * k_; b = bytes.fromhex(sq['hex'])
    else:
        a = sq['addr']; b = img.read(a, sq['end'] - a)
    il = list(md.disasm(b, a))
    if sum(x.size for x in il) != len(b):
        return None, 'does not decode to whole instructions'
    end = a + len(b)
    insns = {x.address: x for x in il}
    kind = {'K_SEQ'} | set(sq.get('kind', []))
    target = 0; dsize = 0
    mems = []
    for x in il:
        m = x.mnemonic.split()[-1]; ops = x.operands
        if 0xd8 <= x.opcode[0] <= 0xdf: kind.add('K_X87')
        if (m.startswith('j') or m.startswith('loop')) and m != 'jmp' and ops and ops[0].type == X86_OP_IMM \
           or m == 'jmp' and ops[0].type == X86_OP_IMM:
            t = ops[0].imm & 0xffffffff
            if t not in insns and t != end:
                if target and t != target: return None, 'two outside branch targets'
                if a - 6 < t < end + 6: return None, 'branch target overlaps the code'
                target = t; kind.add('K_BR')
        elif m in ('call', 'ret', 'jmp') and x is not il[-1]:
            return None, 'call/ret/indirect jump must be last'
        str_op = m[:-1] in STRING and m[-1] in 'bwd' and not 0xd8 <= x.opcode[0] <= 0xdf
        if str_op:
            kind.add('K_STRING')
            if x.prefix[0] in (0xf2, 0xf3): kind.add('K_REP')
        elif m not in ('lea', 'nop'):
            mems += [(x, o) for o in ops if o.type == X86_OP_MEM]
        if m in ('div', 'idiv'):
            kind.add('K_DIV'); dsize = ops[0].size
            if m == 'idiv': kind.add('K_IDIV')
    fake = dict(insns); fake[end] = None
    if target: fake[target] = None
    em = L.Emitter(FakeF(fake))
    body = []
    try:
        for x in il:
            em.out = []
            em.emit(x)
            body.append('L_%08x: ;' % x.address)
            body += em.out
    except L.LiftError as ex_:
        return None, 'lifter rejects: %s' % ex_
    # memory: the first operand drives the register choice; a second absolute address gets its own page
    mrow = '0, -1, -1, 0, 0u, 0, MF_INT'; xabs = 0
    if mems:
        x, o = mems[0]; mo = o.mem
        base = REGIDX[L.reg32(x.reg_name(mo.base))] if mo.base else -1
        index = REGIDX[L.reg32(x.reg_name(mo.index))] if mo.index else -1
        mf = x87_mfmt(bytes(x.bytes)) if 0xd8 <= x.opcode[0] <= 0xdf else 'MF_INT'
        mrow = '1, %d, %d, %d, 0x%xu, %d, %s' % (base, index, mo.scale if mo.index else 0, mo.disp & 0xffffffff, o.size, mf)
        drv_abs = not (mo.base or mo.index)
        for x2, o2 in mems[1:]:
            m2 = o2.mem
            if not (m2.base or m2.index) and not (drv_abs and (m2.disp & ~0xfff) == (mo.disp & ~0xfff)):
                xabs = m2.disp & 0xffffffff
    c = ['void fzl_%d(void)' % i, '{',
         '    u32 eax=R.eax,ecx=R.ecx,edx=R.edx,ebx=R.ebx,esp=R.esp,ebp=R.ebp,esi=R.esi,edi=R.edi;',
         '    u8 cf=FZF.cf,zf=FZF.zf,sf=FZF.sf,of=FZF.of,pf=FZF.pf;',
         '    /* %s */' % sq['name']]
    c += [l.replace('return;', '{ if (!FZ_exit) FZ_exit = 2; goto fz_out; }') for l in body]
    c.append('L_%08x: goto fz_out;' % end)
    if target: c.append('L_%08x: FZ_exit = 1; goto fz_out;' % target)
    c += ['fz_out:', '    SAVE; FZF.cf=cf; FZF.zf=zf; FZF.sf=sf; FZF.of=of; FZF.pf=pf;', '}']
    k = ['void fzm_%d(void)' % i, '{',
         '    u32 eax=FZI[0],ecx=FZI[1],edx=FZI[2],ebx=FZI[3],esp=FZI[4],ebp=FZI[5],esi=FZI[6],edi=FZI[7];',
         '    (void)eax;(void)ecx;(void)edx;(void)ebx;(void)esp;(void)ebp;(void)esi;(void)edi;',
         '    ' + sq.get('mask', ''), '}']
    hexb = '"' + ''.join('\\x%02x' % v for v in b) + '"'
    text = ' ; '.join('%s %s' % (x.mnemonic, x.op_str) for x in il)
    row = '{%s, %s, 0x%08xu, %d, %s, fzl_%d, fzm_%d, %s, %s, 0x%08xu, -1, -1, %d, 0x%08xu}' % (
        cstr('seq: ' + sq['name']), cstr(text[:200]), a, len(b), hexb, i, i, '|'.join(sorted(kind)), mrow, target, dsize, xabs)
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
    # extra checks from pcrecomp's lifter bug list (reports/pcrecomp-faults.md):
    # single forms the game does not use, then multi-instruction sequences
    nextra = nseq = 0
    for k_, h in enumerate(EXTRA):
        ex = {'addr': '%08x' % (EXTRA_BASE + 0x1000 * k_), 'bytes': h}
        ins = next(md.disasm(bytes.fromhex(h), int(ex['addr'], 16), 1))
        ex['text'] = '%s %s' % (ins.mnemonic, ins.op_str)
        key = 'extra: ' + form_key(ins)
        code, row = gen_example(i, key, ex, md)
        if code is None:
            skips.setdefault(key, (row, 0, ex['text']))
            continue
        codes.append(code); rows.append(row); keys.add(key); i += 1; nextra += 1
    img = L.Image(L.EXE)
    for k_, sq in enumerate(SEQS):
        code, row = gen_seq(i, k_, sq, md, img)
        key = 'seq: ' + sq['name']
        if code is None:
            skips.setdefault(key, (row, 0, sq['name']))
            continue
        codes.append(code); rows.append(row); keys.add(key); i += 1; nseq += 1
    print('extra forms (not in game.exe) %d of %d, sequences %d of %d' % (nextra, len(EXTRA), nseq, len(SEQS)))
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

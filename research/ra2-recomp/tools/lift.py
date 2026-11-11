#!/usr/bin/env python3
"""lift.py: mechanical x86-32 -> C lifter for game.exe 1.006.
Each x86 instruction becomes C that does the same register, flag and memory work.
usage: lift.py <out-dir> <hex-address>...   (writes <out-dir>/f_<addr>.c, prints a JSON status line per function)
"""
import json, os, struct, sys, time
from capstone import Cs, CS_ARCH_X86, CS_MODE_32
from capstone.x86 import *

HERE = os.path.dirname(os.path.abspath(__file__))
EXE = os.environ.get('RC_EXE', '/path/to/user/Projects/ra2/ra2-1to1/inputs/game.exe')
ENTRIES = os.environ.get('RC_ENTRIES', '/path/to/user/Projects/ra2/ra2-1to1/inputs/all-addresses.txt')

class Image:
    def __init__(self, path):
        d = open(path, 'rb').read()
        pe = struct.unpack_from('<I', d, 0x3c)[0]
        nsec = struct.unpack_from('<H', d, pe + 6)[0]
        opt = struct.unpack_from('<H', d, pe + 20)[0]
        self.base = struct.unpack_from('<I', d, pe + 24 + 28)[0]
        self.secs = []
        s = pe + 24 + opt
        for i in range(nsec):
            name = d[s:s + 8].rstrip(b'\0').decode()
            vs, va, rs, ra = struct.unpack_from('<IIII', d, s + 8)
            self.secs.append((name, self.base + va, vs, ra, rs))
            s += 40
        self.d = d
        t = [x for x in self.secs if x[0] == '.text'][0]
        self.text_lo, self.text_hi = t[1], t[1] + t[2]
    def read(self, va, n):
        for name, a, vs, ra, rs in self.secs:
            if a <= va < a + vs:
                off = va - a
                b = self.d[ra + off: ra + min(off + n, rs)]
                return b + b'\0' * (n - len(b))
        return None
    def u32(self, va):
        b = self.read(va, 4)
        return struct.unpack('<I', b)[0] if b else None
    def u8(self, va):
        b = self.read(va, 1)
        return b[0] if b else None

class LiftError(Exception):
    pass

REG32 = ['eax', 'ecx', 'edx', 'ebx', 'esp', 'ebp', 'esi', 'edi']
MASK = {1: '0xffu', 2: '0xffffu', 4: '0xffffffffu'}
BITS = {1: 8, 2: 16, 4: 32}
STY = {1: 's8', 2: 's16', 4: 's32'}
UTY = {1: 'u8', 2: 'u16', 4: 'u32'}
M = {1: 'M8', 2: 'M16', 4: 'M32'}
CC = {
    'o': 'of', 'no': '!of', 'b': 'cf', 'ae': '!cf', 'e': 'zf', 'ne': '!zf', 'be': '(cf|zf)', 'a': '(!cf&&!zf)',
    's': 'sf', 'ns': '!sf', 'p': 'pf', 'np': '!pf', 'l': '(sf!=of)', 'ge': '(sf==of)', 'le': '(zf||sf!=of)', 'g': '(!zf&&sf==of)',
}
CCFLAGS = {'o': 'o', 'no': 'o', 'b': 'c', 'ae': 'c', 'e': 'z', 'ne': 'z', 'be': 'cz', 'a': 'cz', 's': 's', 'ns': 's',
           'p': 'p', 'np': 'p', 'l': 'so', 'ge': 'so', 'le': 'zso', 'g': 'zso'}
ALLF = 'czsop'

class Func:
    def __init__(self, img, md, entry, entries):
        self.img, self.md, self.entry, self.entries = img, md, entry, entries
        self.insns = {}       # addr -> capstone insn
        self.succ = {}        # addr -> list of successor addrs inside the function
        self.targets = set()  # label addresses
        self.tables = {}      # addr of jmp -> list of targets
        self.calls = set()    # direct call targets
        self.bad = {}         # addr -> reason (emit trap)
        self.notes = []

    def decode(self, a):
        b = self.img.read(a, 16)
        if b is None or not (self.img.text_lo <= a < self.img.text_hi):
            return None
        for ins in self.md.disasm(b, a, 1):
            return ins
        return None

    def discover(self):
        work = [self.entry]
        while work:
            a = work.pop()
            if a in self.insns or a in self.bad:
                continue
            ins = self.decode(a)
            if ins is None:
                self.bad[a] = 'cannot decode'
                continue
            self.insns[a] = ins
            m = ins.mnemonic
            nxt = a + ins.size
            succ = []
            if m == 'jmp':
                op = ins.operands[0]
                if op.type == X86_OP_IMM:
                    t = op.imm & 0xffffffff
                    if t in self.entries and t != self.entry:
                        pass  # tail call
                    else:
                        succ = [t]
                elif op.type == X86_OP_MEM and op.mem.index != 0 and op.mem.scale == 4 and op.mem.base == 0 and op.mem.segment == 0:
                    tl = self.jump_table(a, ins)
                    self.tables[a] = tl
                    succ = sorted(set(tl))
                # other indirect jumps: tail call through a pointer
            elif m.startswith('j') or m in ('loop', 'loope', 'loopne'):
                t = ins.operands[0].imm & 0xffffffff
                succ = [t, nxt]
            elif m in ('ret', 'retf', 'int3', 'hlt', 'ud2'):
                succ = []
            elif m == 'call':
                op = ins.operands[0]
                if op.type == X86_OP_IMM:
                    t = op.imm & 0xffffffff
                    self.calls.add(t)
                    if t in NORETURN:
                        succ = []
                    else:
                        succ = [nxt]
                else:
                    succ = [nxt]
                if succ and (nxt in self.entries and nxt != self.entry):
                    # fall-through runs into the next function: treat as no-return
                    self.bad[nxt] = 'falls into next function after call'
                    succ = []
            else:
                if nxt in self.entries and nxt != self.entry:
                    succ = []
                    self.bad[nxt] = 'falls into next function'
                else:
                    succ = [nxt]
            # successors must not be other function entries reached by jumps
            self.succ[a] = succ
            for s in succ:
                if s != nxt or m.startswith('j') or m in ('loop', 'loope', 'loopne'):
                    self.targets.add(s)
                work.append(s)
        for a in list(self.succ):
            for s in self.succ[a]:
                if s in self.bad:
                    self.targets.add(s)

    def jump_table(self, a, ins):
        mem = ins.operands[0].mem
        tbl = mem.disp & 0xffffffff
        idx = ins.reg_name(mem.index)
        # look back for "cmp idx, N" / "ja" and a byte table load
        prev = self.linear_prev(a, 12)
        n = None; btable = None; breg = None
        for p in reversed(prev):
            if p.mnemonic in ('movzx', 'mov') and len(p.operands) == 2 and p.operands[1].type == X86_OP_MEM and p.operands[1].size == 1 \
               and p.operands[0].type == X86_OP_REG and reg32(p.reg_name(p.operands[0].reg)) == idx and btable is None and n is None:
                bm = p.operands[1].mem
                if bm.index == 0 and bm.base != 0 and (bm.disp & 0xffffffff) >= self.img.base:
                    btable = bm.disp & 0xffffffff
                    breg = reg32(p.reg_name(bm.base))
                    continue
            if p.mnemonic == 'cmp' and p.operands[0].type == X86_OP_REG and p.operands[1].type in (X86_OP_IMM, X86_OP_REG):
                r = reg32(p.reg_name(p.operands[0].reg))
                if r == (breg or idx):
                    if p.operands[1].type == X86_OP_IMM:
                        n = (p.operands[1].imm & 0xffffffff) + 1
                    else:
                        k = self.reg_const(prev, p, reg32(p.reg_name(p.operands[1].reg)))
                        if k is not None:
                            n = k + 1
                    break
            if p.mnemonic == 'and' and p.operands[0].type == X86_OP_REG and p.operands[1].type == X86_OP_IMM and n is None \
               and reg32(p.reg_name(p.operands[0].reg)) == (breg or idx) and (p.operands[1].imm & 0xffffffff) < 512:
                n = (p.operands[1].imm & 0xffffffff) + 1
                break
        if btable is not None:
            if n is None:
                # no bound check: the caller keeps the index in range. The dword table
                # ends where the byte table starts (or at the first invalid target).
                k = 0
                while k < 256 and (btable <= tbl or tbl + 4 * k < btable):
                    t = self.img.u32(tbl + 4 * k)
                    if t is None or not (self.img.text_lo <= t < self.img.text_hi) or abs(t - a) > 0x10000:
                        break
                    k += 1
                if k == 0:
                    raise LiftError('byte jump table without bound at %08x' % a)
                self.notes.append('byte jump table %08x: no bound, %d targets read' % (a, k))
                return [self.img.u32(tbl + 4 * i) for i in range(k)]
            n = max(self.img.u8(btable + i) for i in range(n)) + 1
        if n is None:
            # no bound found: read while entries point into this area of .text
            n = 0
            while n < 512:
                t = self.img.u32(tbl + 4 * n)
                if t is None or not (self.img.text_lo <= t < self.img.text_hi) or abs(t - a) > 0x10000:
                    break
                n += 1
            self.notes.append('jump table %08x: no bound, %d entries read' % (a, n))
            if n == 0:
                raise LiftError('empty jump table at %08x' % a)
        return [self.img.u32(tbl + 4 * i) for i in range(n)]

    def reg_const(self, prev, upto, r):
        """Constant value of register r just before instruction upto (mov r, imm / push imm; pop r)."""
        seen = [p for p in prev if p.address < upto.address]
        for j in range(len(seen) - 1, -1, -1):
            p = seen[j]
            ops = p.operands
            if p.mnemonic == 'mov' and len(ops) == 2 and ops[0].type == X86_OP_REG and reg32(p.reg_name(ops[0].reg)) == r:
                return (ops[1].imm & 0xffffffff) if ops[1].type == X86_OP_IMM and ops[0].size == 4 else None
            if p.mnemonic == 'pop' and ops[0].type == X86_OP_REG and reg32(p.reg_name(ops[0].reg)) == r:
                for q in reversed(seen[:j]):       # matching push, no other stack change between
                    if q.mnemonic == 'push':
                        return (q.operands[0].imm & 0xffffffff) if q.operands[0].type == X86_OP_IMM else None
                    if q.mnemonic in ('pop', 'call', 'ret', 'enter', 'leave', 'pushal', 'popal', 'pushfd', 'popfd') or \
                       any(o.type == X86_OP_REG and reg32(q.reg_name(o.reg)) == 'esp' for o in q.operands):
                        return None
                return None
            if len(ops) and ops[0].type == X86_OP_REG and reg32(p.reg_name(ops[0].reg)) == r and p.mnemonic not in ('cmp', 'test', 'push'):
                return None
        return None

    def linear_prev(self, a, k):
        out = []
        lo = a - 64
        # decode forward from earlier known instructions in this function
        known = sorted(x for x in self.insns if lo <= x < a)
        if not known:
            return out
        p = known[0]
        while p < a:
            ins = self.insns.get(p) or self.decode(p)
            if ins is None:
                break
            out.append(ins)
            p += ins.size
        return out[-k:] if p == a else [i for i in out if i.address in self.insns][-k:]

def reg32(r):
    if r in REG32: return r
    t = {'al': 'eax', 'ah': 'eax', 'ax': 'eax', 'cl': 'ecx', 'ch': 'ecx', 'cx': 'ecx', 'dl': 'edx', 'dh': 'edx', 'dx': 'edx',
         'bl': 'ebx', 'bh': 'ebx', 'bx': 'ebx', 'sp': 'esp', 'bp': 'ebp', 'si': 'esi', 'di': 'edi'}
    return t.get(r, r)

NORETURN = set()
# Callees that return a result in the flags (CRT asm helpers). Callers read the flags
# after the call; the callee packs its flags into R.eflags at ret (native: the bridge).
FLAGRET = {0x787665}

class Emitter:
    def __init__(self, f):
        self.f = f
        self.ins = None
        self.out = []
        self.uses_fs = False
        self.uses_mmx = False

    def e(self, s):
        self.out.append('    ' + s)

    # ---- operands ----
    def ea(self, op):
        m = op.mem
        parts = []
        if m.base:
            parts.append(reg32(self.ins.reg_name(m.base)))
        if m.index:
            r = reg32(self.ins.reg_name(m.index))
            parts.append(r if m.scale == 1 else '%s*%du' % (r, m.scale))
        if m.disp or not parts:
            parts.append('0x%xu' % (m.disp & 0xffffffff))
        e = '(u32)(' + '+'.join(parts) + ')'
        seg = self.ins.reg_name(m.segment) if m.segment else None
        if seg == 'fs':
            return ('fs', e)
        if seg not in (None, 'ds', 'ss', 'es', 'cs'):
            raise LiftError('segment %s' % seg)
        return e

    def rd(self, op, size=None):
        size = size or op.size
        if op.type == X86_OP_REG:
            return self.rreg(self.ins.reg_name(op.reg))
        if op.type == X86_OP_IMM:
            return '0x%xu' % (op.imm & int(MASK[size][:-1], 16))
        a = self.ea(op)
        if isinstance(a, tuple):
            if size != 4: raise LiftError('fs access size %d' % size)
            return 'FS32(%s)' % a[1]
        return '(u32)%s(%s)' % (M[size], a)

    def wr(self, op, v, size=None):
        size = size or op.size
        if op.type == X86_OP_REG:
            self.wreg(self.ins.reg_name(op.reg), v)
            return
        a = self.ea(op)
        if isinstance(a, tuple):
            if size != 4: raise LiftError('fs access size %d' % size)
            self.e('FSW32(%s, %s);' % (a[1], v))
            return
        self.e('%s(%s) = (%s)(%s);' % (M[size], a, UTY[size], v))

    def rreg(self, r):
        if r in REG32: return r
        if r in ('al', 'cl', 'dl', 'bl'): return '(%s & 0xffu)' % reg32(r)
        if r in ('ah', 'ch', 'dh', 'bh'): return '((%s >> 8) & 0xffu)' % reg32(r)
        if r in ('ax', 'cx', 'dx', 'bx', 'sp', 'bp', 'si', 'di'): return '(%s & 0xffffu)' % reg32(r)
        raise LiftError('register %s' % r)

    def wreg(self, r, v):
        if r in REG32:
            self.e('%s = (u32)(%s);' % (r, v)); return
        R32 = reg32(r)
        if r in ('al', 'cl', 'dl', 'bl'):
            self.e('%s = (%s & 0xffffff00u) | ((u32)(%s) & 0xffu);' % (R32, R32, v))
        elif r in ('ah', 'ch', 'dh', 'bh'):
            self.e('%s = (%s & 0xffff00ffu) | (((u32)(%s) & 0xffu) << 8);' % (R32, R32, v))
        elif r in ('ax', 'cx', 'dx', 'bx', 'sp', 'bp', 'si', 'di'):
            self.e('%s = (%s & 0xffff0000u) | ((u32)(%s) & 0xffffu);' % (R32, R32, v))
        else:
            raise LiftError('register %s' % r)

    def dst_ref(self, op):
        """For read-modify-write: compute the address once. Returns (read_expr, write_fn)."""
        if op.type == X86_OP_MEM:
            a = self.ea(op)
            if isinstance(a, tuple):
                self.e('u32 ad = %s;' % a[1])
                return 'FS32(ad)', (lambda v: self.e('FSW32(ad, %s);' % v))
            self.e('u32 ad = %s;' % a)
            s = op.size
            return '(u32)%s(ad)' % M[s], (lambda v: self.e('%s(ad) = (%s)(%s);' % (M[s], UTY[s], v)))
        return self.rd(op), (lambda v: self.wr(op, v))

    def szp(self, r, size):
        self.e('zf = ((%s) & %s) == 0; sf = ((%s) >> %d) & 1; pf = PAR(%s);' % (r, MASK[size], r, BITS[size] - 1, r))

    def push(self, v):
        self.e('{ u32 pv = (u32)(%s); esp -= 4; M32(esp) = pv; }' % v)

    def lab(self, t):
        return 'L_%08x' % t

    def jump(self, t):
        f = self.f
        if t in f.insns or t in f.bad:
            return 'goto %s;' % self.lab(t)
        # tail call to another function
        return '{ SAVE; rc_call(0x%08xu); return; }' % t

    # ---- instructions ----
    def emit(self, ins):
        self.ins = ins
        m = ins.mnemonic.split()[-1]
        ops = ins.operands
        a = ins.address
        nxt = a + ins.size
        pre = ins.prefix[0]
        b0 = ins.bytes[0]
        if 0xd8 <= ins.opcode[0] <= 0xdf and m not in ('wait',):
            return self.x87(ins)
        if m in ('nop', 'wait', 'fwait'):
            return
        if m == 'mov':
            self.wr(ops[0], self.rd(ops[1]))
        elif m in ('movzx', 'movsx'):
            v = self.rd(ops[1])
            if m == 'movsx':
                v = '(u32)(s32)(%s)(%s)' % (STY[ops[1].size], v)
            self.wr(ops[0], v)
        elif m == 'lea':
            a2 = self.ea(ops[1])
            if isinstance(a2, tuple): raise LiftError('lea fs')
            self.wr(ops[0], a2)
        elif m == 'push':
            if ops[0].type == X86_OP_IMM:
                self.push('0x%xu' % (ops[0].imm & 0xffffffff))
            else:
                if ops[0].size != 4: raise LiftError('push16')
                self.push(self.rd(ops[0]))
        elif m == 'pop':
            if ops[0].size != 4: raise LiftError('pop16')
            if ops[0].type == X86_OP_REG and self.ins.reg_name(ops[0].reg) == 'esp':
                self.e('esp = M32(esp);')
            else:
                self.e('{ u32 pv = M32(esp); esp += 4;')
                self.wr(ops[0], 'pv')
                self.e('}')
        elif m in ('add', 'sub', 'cmp', 'adc', 'sbb'):
            s = ops[0].size
            self.e('{')
            av, w = self.dst_ref(ops[0])
            bv = self.rd(ops[1], s)
            mk = MASK[s]; sh = BITS[s] - 1
            self.e('u32 a = %s, b = (%s) & %s;' % (av, bv, mk))
            if m == 'add':
                self.e('u32 r = (a + b) & %s; cf = r < a;' % mk)
                self.e('of = (((a ^ r) & (b ^ r)) >> %d) & 1;' % sh)
            elif m == 'adc':
                self.e('u64 w = (u64)a + b + cf; u32 r = (u32)w & %s; cf = (w >> %d) & 1;' % (mk, sh + 1))
                self.e('of = (((a ^ r) & (b ^ r)) >> %d) & 1;' % sh)
            elif m in ('sub', 'cmp'):
                self.e('u32 r = (a - b) & %s; cf = a < b;' % mk)
                self.e('of = (((a ^ b) & (a ^ r)) >> %d) & 1;' % sh)
            else:
                self.e('u32 c = cf; u32 r = (a - b - c) & %s; cf = (u64)a < (u64)b + c;' % mk)
                self.e('of = (((a ^ b) & (a ^ r)) >> %d) & 1;' % sh)
            self.szp('r', s)
            if m != 'cmp':
                w('r')
            self.e('}')
        elif m in ('and', 'or', 'xor', 'test'):
            s = ops[0].size
            op = {'and': '&', 'or': '|', 'xor': '^', 'test': '&'}[m]
            self.e('{')
            if m != 'test' and ops[0].type == X86_OP_REG and ops[1].type == X86_OP_REG and ops[0].reg == ops[1].reg and m == 'xor':
                av, w = '0', (lambda v: self.wr(ops[0], v))
                self.e('u32 r = 0;')
            else:
                av, w = self.dst_ref(ops[0])
                self.e('u32 r = (%s %s (%s)) & %s;' % (av, op, self.rd(ops[1], s), MASK[s]))
            self.e('cf = 0; of = 0;')
            self.szp('r', s)
            if m != 'test':
                w('r')
            self.e('}')
        elif m in ('inc', 'dec'):
            s = ops[0].size
            self.e('{')
            av, w = self.dst_ref(ops[0])
            sb = '0x%xu' % (1 << (BITS[s] - 1))
            if m == 'inc':
                self.e('u32 r = (%s + 1) & %s; of = r == %s;' % (av, MASK[s], sb))
            else:
                self.e('u32 a = %s; u32 r = (a - 1) & %s; of = a == %s;' % (av, MASK[s], sb))
            self.szp('r', s)
            w('r')
            self.e('}')
        elif m == 'neg':
            s = ops[0].size
            self.e('{')
            av, w = self.dst_ref(ops[0])
            self.e('u32 a = %s & %s; u32 r = (0u - a) & %s; cf = a != 0; of = a == 0x%xu;' % (av, MASK[s], MASK[s], 1 << (BITS[s] - 1)))
            self.szp('r', s)
            w('r')
            self.e('}')
        elif m == 'not':
            s = ops[0].size
            self.e('{')
            av, w = self.dst_ref(ops[0])
            w('~(%s)' % av)
            self.e('}')
        elif m in ('shl', 'sal', 'shr', 'sar', 'rol', 'ror', 'rcl', 'rcr'):
            self.shift(ins, m)
        elif m in ('shld', 'shrd'):
            self.shiftd(ins, m)
        elif m == 'imul':
            self.imul(ins)
        elif m == 'mul':
            s = ops[0].size
            v = self.rd(ops[0])
            if s == 4:
                self.e('{ u64 r = (u64)eax * (u32)(%s); eax = (u32)r; edx = (u32)(r >> 32); cf = of = edx != 0; zf = eax == 0; sf = eax >> 31; pf = PAR(eax); }' % v)
            elif s == 2:
                self.e('{ u32 r = (eax & 0xffffu) * ((%s) & 0xffffu); eax = (eax & 0xffff0000u) | (r & 0xffffu); edx = (edx & 0xffff0000u) | (r >> 16); cf = of = (r >> 16) != 0; zf = (r & 0xffffu) == 0; sf = (r >> 15) & 1; pf = PAR(r); }' % v)
            else:
                self.e('{ u32 r = (eax & 0xffu) * ((%s) & 0xffu); eax = (eax & 0xffff0000u) | (r & 0xffffu); cf = of = (r >> 8) != 0; zf = (r & 0xffu) == 0; sf = (r >> 7) & 1; pf = PAR(r); }' % v)
        elif m in ('div', 'idiv'):
            s = ops[0].size
            v = self.rd(ops[0])
            if s == 4:
                if m == 'div':
                    self.e('{ u32 d = %s; __asm__ volatile("divl %%4" : "=a"(eax), "=d"(edx) : "0"(eax), "1"(edx), "rm"(d)); }' % v)
                else:
                    self.e('{ u32 d = %s; __asm__ volatile("idivl %%4" : "=a"(eax), "=d"(edx) : "0"(eax), "1"(edx), "rm"(d)); }' % v)
            elif s == 2:
                ins_ = 'divw' if m == 'div' else 'idivw'
                self.e('{ u16 d = (u16)(%s); u16 lo = (u16)eax, hi = (u16)edx; __asm__ volatile("%s %%4" : "=a"(lo), "=d"(hi) : "0"(lo), "1"(hi), "rm"(d)); eax = (eax & 0xffff0000u) | lo; edx = (edx & 0xffff0000u) | hi; }' % (v, ins_))
            else:
                ins_ = 'divb' if m == 'div' else 'idivb'
                self.e('{ u8 d = (u8)(%s); u16 ax = (u16)eax; __asm__ volatile("%s %%2" : "=a"(ax) : "0"(ax), "qm"(d)); eax = (eax & 0xffff0000u) | ax; }' % (v, ins_))
        elif m == 'cdq':
            self.e('edx = (u32)((s32)eax >> 31);')
        elif m == 'cwde':
            self.e('eax = (u32)(s32)(s16)eax;')
        elif m == 'cbw':
            self.e('eax = (eax & 0xffff0000u) | (u16)(s16)(s8)eax;')
        elif m == 'cwd':
            self.e('edx = (edx & 0xffff0000u) | (((s16)eax < 0) ? 0xffffu : 0);')
        elif m == 'xchg':
            self.e('{')
            if ops[0].type == X86_OP_MEM or ops[1].type == X86_OP_MEM:
                mo, ro = (ops[0], ops[1]) if ops[0].type == X86_OP_MEM else (ops[1], ops[0])
                av, w = self.dst_ref(mo)
                self.e('u32 t = %s;' % av)
                w(self.rd(ro))
                self.wr(ro, 't')
            else:
                self.e('u32 t = %s;' % self.rd(ops[0]))
                self.wr(ops[0], self.rd(ops[1]))
                self.wr(ops[1], 't')
            self.e('}')
        elif m == 'bswap':
            r = self.rd(ops[0])
            self.wr(ops[0], '__builtin_bswap32(%s)' % r)
        elif m.startswith('set'):
            self.wr(ops[0], CC[m[3:]] if m[3:] in CC else self.cc_alias(m[3:]))
        elif m.startswith('cmov'):
            c = m[4:]
            self.e('if (%s) {' % (CC[c] if c in CC else self.cc_alias(c)))
            self.wr(ops[0], self.rd(ops[1]))
            self.e('}')
        elif m == 'jmp':
            op = ops[0]
            if op.type == X86_OP_IMM:
                self.e(self.jump(op.imm & 0xffffffff))
            elif a in self.f.tables:
                self.e('{ u32 t = %s;' % self.rd(op))
                self.e('  switch (t) {')
                for t in sorted(set(self.f.tables[a])):
                    self.e('  case 0x%08xu: goto %s;' % (t, self.lab(t)))
                self.e('  default: rc_bad_jump(0x%08xu, t); } }' % a)
            else:
                self.e('{ u32 t = %s; SAVE; rc_call(t); return; }' % self.rd(op))
        elif m.startswith('j') and m != 'jmp':
            c = m[1:]
            t = ops[0].imm & 0xffffffff
            if c == 'ecxz':
                cond = 'ecx == 0'
            elif c == 'cxz':
                cond = '(ecx & 0xffffu) == 0'
            else:
                cond = CC[c] if c in CC else self.cc_alias(c)
            self.e('if (%s) %s' % (cond, self.jump(t)))
        elif m in ('loop',):
            t = ops[0].imm & 0xffffffff
            self.e('ecx--; if (ecx) %s' % self.jump(t))
        elif m == 'call':
            op = ops[0]
            if op.type == X86_OP_IMM:
                t = op.imm & 0xffffffff
                self.e('esp -= 4; M32(esp) = 0x%08xu; SAVE; rc_call(0x%08xu); LOAD;%s' % (nxt, t, ' FLAGS_FROM_R;' if t in FLAGRET else ''))
            else:
                self.e('{ u32 t = %s; esp -= 4; M32(esp) = 0x%08xu; SAVE; rc_call(t); LOAD; }' % (self.rd(op), nxt))
        elif m == 'ret':
            n = ops[0].imm if ops else 0
            if self.f.entry in FLAGRET:
                self.e('R.eflags = (u32)(cf | 2 | pf << 2 | zf << 6 | sf << 7 | of << 11 | 0x200);')
            self.e('esp += %d; SAVE; return;' % (4 + n))
        elif m == 'leave':
            self.e('esp = ebp; ebp = M32(esp); esp += 4;')
        elif m in ('int3', 'hlt', 'ud2'):
            self.e('rc_unreachable(0x%08xu);' % a)
        elif m == 'cld':
            self.e('R.df = 0;')
        elif m == 'std':
            self.e('R.df = 1;')
        elif m == 'clc':
            self.e('cf = 0;')
        elif m == 'stc':
            self.e('cf = 1;')
        elif m == 'cmc':
            self.e('cf = !cf;')
        elif m == 'sahf':
            self.e('{ u32 h = (eax >> 8) & 0xffu; cf = h & 1; pf = (h >> 2) & 1; zf = (h >> 6) & 1; sf = (h >> 7) & 1; }')
        elif m == 'lahf':
            self.e('eax = (eax & 0xffff00ffu) | ((u32)(cf | 2 | pf << 2 | zf << 6 | sf << 7) << 8);')
        elif m in ('bt', 'bts', 'btr', 'btc'):
            self.bt(ins, m)
        elif m in ('bsf', 'bsr'):
            s = ops[0].size
            v = self.rd(ops[1])
            self.e('{ u32 v = (%s) & %s; zf = v == 0; if (v) {' % (v, MASK[s]))
            self.wr(ops[0], ('__builtin_ctz(v)' if m == 'bsf' else '(31 - __builtin_clz(v))'))
            self.e('} }')
        elif m == 'xadd':
            s = ops[0].size
            self.e('{')
            av, w = self.dst_ref(ops[0])
            self.e('u32 a = %s, b = (%s) & %s; u32 r = (a + b) & %s; cf = r < a; of = (((a ^ r) & (b ^ r)) >> %d) & 1;' % (av, self.rd(ops[1]), MASK[s], MASK[s], BITS[s] - 1))
            self.szp('r', s)
            self.wr(ops[1], 'a')
            w('r')
            self.e('}')
        elif m == 'cmpxchg':
            s = ops[0].size
            self.e('{')
            av, w = self.dst_ref(ops[0])
            ax = self.rreg({1: 'al', 2: 'ax', 4: 'eax'}[s])
            self.e('u32 a = %s, d = %s; u32 r = (a - d) & %s; cf = a < d; of = (((a ^ d) & (a ^ r)) >> %d) & 1;' % (ax, av, MASK[s], BITS[s] - 1))
            self.szp('r', s)
            self.e('if (zf) {')
            w(self.rd(ops[1]))
            self.e('} else {')
            self.wreg({1: 'al', 2: 'ax', 4: 'eax'}[s], 'd')
            self.e('}')
            self.e('}')
        elif m in ('pushal', 'pushad'):
            self.e('{ u32 t = esp; esp -= 32; M32(esp+28)=eax; M32(esp+24)=ecx; M32(esp+20)=edx; M32(esp+16)=ebx; M32(esp+12)=t; M32(esp+8)=ebp; M32(esp+4)=esi; M32(esp)=edi; }')
        elif m in ('popal', 'popad'):
            self.e('edi=M32(esp); esi=M32(esp+4); ebp=M32(esp+8); ebx=M32(esp+16); edx=M32(esp+20); ecx=M32(esp+24); eax=M32(esp+28); esp += 32;')
        elif m in ('pushfd', 'pushf'):
            self.e('esp -= 4; M32(esp) = (u32)(cf | 2 | pf << 2 | zf << 6 | sf << 7 | R.df << 10 | of << 11 | 0x200);')
        elif m in ('popfd', 'popf'):
            self.e('{ u32 f = M32(esp); esp += 4; cf = f & 1; pf = (f >> 2) & 1; zf = (f >> 6) & 1; sf = (f >> 7) & 1; R.df = (f >> 10) & 1; of = (f >> 11) & 1; }')
        elif m in ('movsb', 'movsw', 'movsd', 'stosb', 'stosw', 'stosd', 'lodsb', 'lodsw', 'lodsd', 'scasb', 'scasw', 'scasd', 'cmpsb', 'cmpsw', 'cmpsd'):
            self.string(ins, m)
        elif m == 'rdtsc':
            self.e('{ u64 t = rc_rdtsc(); eax = (u32)t; edx = (u32)(t >> 32); }')
        elif m == 'cpuid':
            self.e('rc_cpuid(&eax, &ebx, &ecx, &edx);')
        elif m == 'emms' or any(o.type == X86_OP_REG and ins.reg_name(o.reg).startswith('mm') for o in ops):
            self.mmx(ins, m)
        elif m == 'enter':
            if ops[1].imm != 0: raise LiftError('enter with nesting')
            self.e('esp -= 4; M32(esp) = ebp; ebp = esp; esp -= %du;' % ops[0].imm)
        else:
            raise LiftError('instruction %s %s' % (m, ins.op_str))

    MMX2 = {'pxor': 'a ^ b', 'packssdw': 'mmx_packssdw(a, b)', 'packuswb': 'mmx_packuswb(a, b)',
            'paddusb': 'mmx_paddusb(a, b)', 'pmulhw': 'mmx_pmulhw(a, b)', 'pmullw': 'mmx_pmullw(a, b)',
            'punpckhwd': 'mmx_punpckhwd(a, b)', 'punpcklbw': 'mmx_punpcklbw(a, b)', 'psrlw': 'mmx_psrlw(a, b)',
            'psrad': 'mmx_psrad(a, b)'}
    def mmrd(self, op):
        if op.type == X86_OP_REG:
            r = self.ins.reg_name(op.reg)
            return r if r.startswith('mm') else '(u64)%s' % self.rreg(r)
        if op.type == X86_OP_IMM:
            return '(u64)%du' % (op.imm & 0xff)
        a = self.ea(op)
        if isinstance(a, tuple): raise LiftError('mmx fs')
        return '(*(u64 *)(uintptr_t)%s)' % a if op.size == 8 else '(u64)M32(%s)' % a
    def mmx(self, ins, m):
        """MMX: mm0..mm7 are u64 locals. MMX use sets x87 TOP = 0; emms empties all tags."""
        ops = ins.operands
        self.uses_mmx = True
        if m == 'emms':
            self.e('R.tag = 0; R.top = 0;'); return
        if m in ('movq', 'movd'):
            d, v = ops[0], self.mmrd(ops[1])
            if d.type == X86_OP_REG and self.ins.reg_name(d.reg).startswith('mm'):
                self.e('%s = %s;' % (self.ins.reg_name(d.reg), v if m == 'movq' else '(u64)(u32)(%s)' % v))
            elif d.type == X86_OP_REG:
                self.wreg(self.ins.reg_name(d.reg), '(u32)(%s)' % v)
            else:
                a = self.ea(d)
                if isinstance(a, tuple): raise LiftError('mmx fs')
                if m == 'movq': self.e('*(u64 *)(uintptr_t)%s = %s;' % (a, v))
                else: self.e('M32(%s) = (u32)(%s);' % (a, v))
            return
        if m in self.MMX2:
            d = self.ins.reg_name(ops[0].reg)
            self.e('{ u64 a = %s, b = %s; %s = %s; }' % (d, self.mmrd(ops[1]), d, self.MMX2[m]))
            return
        raise LiftError('mmx instruction %s' % m)

    def cc_alias(self, c):
        al = {'c': 'b', 'nae': 'b', 'nb': 'ae', 'nc': 'ae', 'z': 'e', 'nz': 'ne', 'na': 'be', 'nbe': 'a', 'pe': 'p', 'po': 'np',
              'nge': 'l', 'nl': 'ge', 'ng': 'le', 'nle': 'g'}
        if c in al: return CC[al[c]]
        raise LiftError('condition %s' % c)

    def shift(self, ins, m):
        ops = ins.operands
        s = ops[0].size
        bits = BITS[s]
        mk = MASK[s]
        if len(ops) == 1:
            cnt = '1'
            imm = 1
        elif ops[1].type == X86_OP_IMM:
            imm = ops[1].imm & 31
            cnt = str(imm)
        else:
            imm = None
            cnt = '(ecx & 31)'
        if imm == 0:
            return
        self.e('{')
        av, w = self.dst_ref(ops[0])
        self.e('u32 a = (%s) & %s; u32 c = %s; u32 r = a;' % (av, mk, cnt))
        self.e('if (c) {')
        sh = bits - 1
        if m in ('shl', 'sal'):
            self.e('r = (u32)((u64)a << c) & %s; cf = c <= %d ? (u32)(((u64)a >> (%d - c)) & 1) : 0; of = ((r >> %d) & 1) ^ cf;' % (mk, bits, bits, sh))
            self.szp('r', s)
        elif m == 'shr':
            self.e('r = c < 32 ? (a >> c) : 0; cf = (u32)(((u64)a >> (c - 1)) & 1); of = (a >> %d) & 1;' % sh)
            self.szp('r', s)
        elif m == 'sar':
            self.e('s32 sa = (s32)(%s)a; r = (u32)(sa >> (c > %d ? %d : c)) & %s; cf = (u32)((sa >> (c - 1 > %d ? %d : c - 1)) & 1); of = 0;' % (STY[s], sh, sh, mk, sh, sh))
            self.szp('r', s)
        elif m == 'rol':
            self.e('u32 k = c %% %d; r = k ? ((a << k) | (a >> (%d - k))) & %s : a; cf = r & 1; of = ((r >> %d) & 1) ^ cf;' % (bits, bits, mk, sh))
        elif m == 'ror':
            self.e('u32 k = c %% %d; r = k ? ((a >> k) | (a << (%d - k))) & %s : a; cf = (r >> %d) & 1; of = ((r >> %d) ^ (r >> %d)) & 1;' % (bits, bits, mk, sh, sh, sh - 1))
        elif m in ('rcl', 'rcr'):
            self.e('u32 k = c %% %d; while (k--) {' % (bits + 1))
            if m == 'rcl':
                self.e('  u32 nc = (r >> %d) & 1; r = ((r << 1) | cf) & %s; cf = nc; }' % (sh, mk))
                self.e('of = ((r >> %d) & 1) ^ cf;' % sh)
            else:
                self.e('  u32 nc = r & 1; r = (r >> 1) | ((u32)cf << %d); cf = nc; }' % sh)
                self.e('of = ((r >> %d) ^ (r >> %d)) & 1;' % (sh, sh - 1))
        self.e('}')
        w('r')
        self.e('}')

    def shiftd(self, ins, m):
        ops = ins.operands
        if ops[0].size != 4: raise LiftError('16-bit shld/shrd')
        cnt = str(ops[2].imm & 31) if ops[2].type == X86_OP_IMM else '(ecx & 31)'
        self.e('{')
        av, w = self.dst_ref(ops[0])
        self.e('u32 a = %s, b = %s, c = %s, r = a; if (c) {' % (av, self.rd(ops[1]), cnt))
        if m == 'shld':
            self.e('r = (a << c) | (u32)((u64)b >> (32 - c)); cf = (a >> (32 - c)) & 1; of = ((r ^ a) >> 31) & 1;')
        else:
            self.e('r = (a >> c) | (u32)((u64)b << (32 - c)); cf = (a >> (c - 1)) & 1; of = ((r ^ a) >> 31) & 1;')
        self.szp('r', 4)
        self.e('}')
        w('r')
        self.e('}')

    def imul(self, ins):
        ops = ins.operands
        s = ops[0].size
        if len(ops) == 1:
            v = self.rd(ops[0])
            if s == 4:
                self.e('{ s64 r = (s64)(s32)eax * (s32)(%s); eax = (u32)r; edx = (u32)((u64)r >> 32); cf = of = r != (s64)(s32)r; zf = eax == 0; sf = eax >> 31; pf = PAR(eax); }' % v)
            elif s == 2:
                self.e('{ s32 r = (s32)(s16)eax * (s16)(%s); eax = (eax & 0xffff0000u) | ((u32)r & 0xffffu); edx = (edx & 0xffff0000u) | (((u32)r >> 16) & 0xffffu); cf = of = r != (s16)r; zf = (r & 0xffff) == 0; sf = (r >> 15) & 1; pf = PAR(r); }' % v)
            else:
                self.e('{ s32 r = (s8)eax * (s8)(%s); eax = (eax & 0xffff0000u) | ((u32)r & 0xffffu); cf = of = r != (s8)r; zf = (r & 0xff) == 0; sf = (r >> 7) & 1; pf = PAR(r); }' % v)
            return
        if len(ops) == 2:
            x, y = self.rd(ops[0]), self.rd(ops[1], s)
        else:
            x, y = self.rd(ops[1]), self.rd(ops[2], s)
        st = STY[s]
        self.e('{ s64 r = (s64)(%s)(%s) * (s64)(%s)(%s); cf = of = r != (s64)(%s)r; u32 v = (u32)r & %s;' % (st, x, st, y, st, MASK[s]))
        self.szp('v', s)
        self.wr(ops[0], 'v')
        self.e('}')

    def bt(self, ins, m):
        ops = ins.operands
        s = ops[0].size
        self.e('{')
        if ops[1].type == X86_OP_IMM:
            bit = '%du' % (ops[1].imm % BITS[s])
            av, w = self.dst_ref(ops[0])
        else:
            if ops[0].type == X86_OP_MEM:
                # bit string addressing: offset may reach outside the operand
                a = self.ea(ops[0])
                if isinstance(a, tuple): raise LiftError('bt fs')
                self.e('s32 bo = (s32)%s; u32 ad = %s + (u32)((bo >> 5) * 4);' % (self.rd(ops[1]), a))
                bit = '((u32)bo & 31)'
                av, w = 'M32(ad)', (lambda v: self.e('M32(ad) = (u32)(%s);' % v))
                s = 4
            else:
                bit = '(%s %% %d)' % (self.rd(ops[1]), BITS[s])
                av, w = self.dst_ref(ops[0])
        self.e('u32 a = %s, k = %s; cf = (a >> k) & 1;' % (av, bit))
        if m == 'bts': w('a | (1u << k)')
        elif m == 'btr': w('a & ~(1u << k)')
        elif m == 'btc': w('a ^ (1u << k)')
        self.e('}')

    def string(self, ins, m):
        sz = {'b': 1, 'w': 2, 'd': 4}[m[-1]]
        base = m[:-1]
        p = ins.prefix[0]
        rep = p in (0xf3, 0xf2)
        if ins.operands and any(o.type == X86_OP_MEM and o.mem.segment and ins.reg_name(o.mem.segment) not in ('ds', 'es') for o in ins.operands):
            raise LiftError('string op with segment override')
        st = '(R.df ? -%d : %d)' % (sz, sz)
        A = self.rreg({1: 'al', 2: 'ax', 4: 'eax'}[sz])
        body = []
        if base == 'movs':
            body.append('%s(edi) = %s(esi); esi += %s; edi += %s;' % (M[sz], M[sz], st, st))
        elif base == 'stos':
            body.append('%s(edi) = (%s)%s; edi += %s;' % (M[sz], UTY[sz], A, st))
        elif base == 'lods':
            self.e('/* lods */')
            if rep: raise LiftError('rep lods')
            self.e('{ u32 v = %s(esi);' % M[sz]); self.wreg({1: 'al', 2: 'ax', 4: 'eax'}[sz], 'v'); self.e('esi += %s; }' % st)
            return
        elif base in ('scas', 'cmps'):
            if base == 'scas':
                body.append('u32 a = %s, b = %s(edi); edi += %s;' % (A, M[sz], st))
            else:
                body.append('u32 a = %s(esi), b = %s(edi); esi += %s; edi += %s;' % (M[sz], M[sz], st, st))
            body.append('u32 r = (a - b) & %s; cf = a < b; of = (((a ^ b) & (a ^ r)) >> %d) & 1; zf = r == 0; sf = (r >> %d) & 1; pf = PAR(r);' % (MASK[sz], BITS[sz] - 1, BITS[sz] - 1))
        if not rep:
            self.e('{ ' + ' '.join(body) + ' }')
            return
        if base in ('movs', 'stos'):
            self.e('while (ecx) { ' + ' '.join(body) + ' ecx--; }')
        else:
            stop = '!zf' if p == 0xf3 else 'zf'   # repe stops when ZF=0, repne stops when ZF=1
            self.e('while (ecx) { ' + ' '.join(body) + ' ecx--; if (%s) break; }' % stop)

    # ---- x87 (decoded from the opcode bytes, Intel manual semantics) ----
    def x87(self, ins):
        b = bytes(ins.bytes)
        i = 0
        while b[i] in (0x66, 0x67, 0xf2, 0xf3, 0x2e, 0x36, 0x3e, 0x26, 0x64, 0x65, 0x9b):
            i += 1
        op, modrm = b[i], b[i + 1]
        mod, reg, rm = modrm >> 6, (modrm >> 3) & 7, modrm & 7
        if mod != 3:
            mops = [o for o in ins.operands if o.type == X86_OP_MEM]
            a = self.ea(mops[0])
            if isinstance(a, tuple): raise LiftError('x87 fs')
            self.e('{ u32 ad = %s;' % a)
            self.x87mem(op, reg)
            self.e('}')
        else:
            self.x87reg(op, reg, rm, modrm)

    ARITH = {0: '+', 1: '*', 4: '-', 5: 'r-', 6: '/', 7: 'r/'}

    def arith(self, x, o, y):
        if o.startswith('r'):
            return '(%s) %s (%s)' % (y, o[1], x)
        return '(%s) %s (%s)' % (x, o, y)

    def x87mem(self, op, reg):
        e = self.e
        if op in (0xd8, 0xdc, 0xda, 0xde):
            src = {0xd8: '(f80)MF32(ad)', 0xdc: '(f80)MF64(ad)', 0xda: '(f80)(s32)M32(ad)', 0xde: '(f80)(s16)M16(ad)'}[op]
            if reg in (2, 3):
                e('fcmp(ST(0), %s);' % src)
                if reg == 3: e('fpop();')
            else:
                e('ST(0) = %s;' % self.arith('ST(0)', self.ARITH[reg], src))
            return
        if op == 0xd9:
            if reg == 0: e('fpush((f80)MF32(ad));')
            elif reg == 2: e('MF32(ad) = (float)ST(0);')
            elif reg == 3: e('MF32(ad) = (float)ST(0); fpop();')
            elif reg == 5: e('fldcw(M16(ad));')
            elif reg == 7: e('M16(ad) = fnstcw();')
            else: raise LiftError('x87 d9 /%d (fldenv/fnstenv)' % reg)
            return
        if op == 0xdb:
            if reg == 0: e('fpush((f80)(s32)M32(ad));')
            elif reg == 2: e('M32(ad) = (u32)fist32(ST(0));')
            elif reg == 3: e('M32(ad) = (u32)fist32(ST(0)); fpop();')
            elif reg == 5: e('fpush(LD80(ad));')
            elif reg == 7: e('ST80(ad, ST(0)); fpop();')
            else: raise LiftError('x87 db /%d' % reg)
            return
        if op == 0xdd:
            if reg == 0: e('fpush((f80)MF64(ad));')
            elif reg == 2: e('MF64(ad) = (double)ST(0);')
            elif reg == 3: e('MF64(ad) = (double)ST(0); fpop();')
            elif reg == 7: e('M16(ad) = fsw();')
            elif reg == 4: e('rc_frstor(ad);')
            elif reg == 6: e('rc_fnsave(ad);')
            else: raise LiftError('x87 dd /%d' % reg)
            return
        if op == 0xdf:
            if reg == 0: e('fpush((f80)(s16)M16(ad));')
            elif reg == 2: e('M16(ad) = (u16)fist16(ST(0));')
            elif reg == 3: e('M16(ad) = (u16)fist16(ST(0)); fpop();')
            elif reg == 5: e('{ s64 v; memcpy(&v, (void *)(uintptr_t)ad, 8); fpush((f80)v); }')
            elif reg == 7: e('{ s64 v = fist64(ST(0)); memcpy((void *)(uintptr_t)ad, &v, 8); fpop(); }')
            else: raise LiftError('x87 df /%d (bcd)' % reg)
            return
        raise LiftError('x87 %02x' % op)

    def x87reg(self, op, reg, rm, modrm):
        e = self.e
        i = rm
        if op == 0xd8:
            if reg in (2, 3):
                e('fcmp(ST(0), ST(%d));' % i)
                if reg == 3: e('fpop();')
            else:
                e('ST(0) = %s;' % self.arith('ST(0)', self.ARITH[reg], 'ST(%d)' % i))
            return
        if op in (0xdc, 0xde):
            if op == 0xde and modrm == 0xd9:
                e('fcmp(ST(0), ST(1)); fpop(); fpop();'); return
            if reg in (2, 3):
                raise LiftError('x87 %02x %02x' % (op, modrm))
            # DC/DE: ST(i) = ST(i) op ST(0); E0=fsubr, E8=fsub, F0=fdivr, F8=fdiv
            o = {0: '+', 1: '*', 4: 'r-', 5: '-', 6: 'r/', 7: '/'}[reg]
            e('ST(%d) = %s;' % (i, self.arith('ST(%d)' % i, o, 'ST(0)')))
            if op == 0xde: e('fpop();')
            return
        if op == 0xd9:
            if reg == 0: e('{ f80 v = ST(%d); fpush(v); }' % i); return
            if reg == 1: e('{ f80 t = ST(0); ST(0) = ST(%d); ST(%d) = t; R.tag |= 1u << R.top | 1u << ((R.top + %d) & 7); }' % (i, i, i)); return
            x = {0xd0: '', 0xe0: 'ST(0) = -ST(0);', 0xe1: 'ST(0) = __builtin_fabsl(ST(0));', 0xe4: 'fcmp(ST(0), 0.0L);',
                 0xe5: 'fxam();',
                 0xe8: 'fpush(1.0L);', 0xe9: 'fpush(3.32192809488736234781L);', 0xea: 'fpush(1.44269504088896340739L);',
                 0xeb: 'fpush(3.14159265358979323851L);', 0xec: 'fpush(0.301029995663981195214L);', 0xed: 'fpush(0.693147180559945309429L);',
                 0xee: 'fpush(0.0L);',
                 0xf0: 'ST(0) = x_f2xm1(ST(0));', 0xf1: 'ST(1) = x_fyl2x(ST(1), ST(0)); fpop();',
                 0xf2: 'ST(0) = x_fptan(ST(0)); fpush(1.0L); R.c2 = 0;', 0xf3: 'ST(1) = x_fpatan(ST(1), ST(0)); fpop();',
                 0xf5: 'ST(0) = x_fprem(ST(0), ST(1), 1);', 0xf8: 'ST(0) = x_fprem(ST(0), ST(1), 0);',
                 0xf9: 'ST(1) = x_fyl2xp1(ST(1), ST(0)); fpop();',
                 0xfa: 'ST(0) = x_fsqrt(ST(0));', 0xfb: '{ f80 v = ST(0); ST(0) = x_fsin(v); fpush(x_fcos(v)); R.c2 = 0; }',
                 0xfc: 'ST(0) = x_frndint(ST(0));', 0xfd: 'ST(0) = x_fscale(ST(0), ST(1));',
                 0xfe: 'ST(0) = x_fsin(ST(0)); R.c2 = 0;', 0xff: 'ST(0) = x_fcos(ST(0)); R.c2 = 0;',
                 0xf6: 'R.top = (R.top - 1) & 7;', 0xf7: 'R.top = (R.top + 1) & 7;'}.get(modrm)
            if x is None: raise LiftError('x87 d9 %02x' % modrm)
            if x and modrm in (0xe8, 0xe9, 0xea, 0xeb, 0xec, 0xed):
                # these constants depend on the rounding mode: load them with the real instruction
                insn = {0xe8: 'fld1', 0xe9: 'fldl2t', 0xea: 'fldl2e', 0xeb: 'fldpi', 0xec: 'fldlg2', 0xed: 'fldln2'}[modrm]
                x = '{ f80 v; __asm__ volatile("%s" : "=t"(v) :: "memory"); fpush(v); }' % insn
            e(x)
            return
        if op == 0xda and modrm == 0xe9:
            e('fcmp(ST(0), ST(1)); fpop(); fpop();'); return
        if op == 0xdb:
            if modrm == 0xe2:
                e('__asm__ volatile("fnclex" ::: "memory");'); return
            if modrm in (0xe0, 0xe1, 0xe4): return   # feni/fdisi/fsetpm: no effect on this CPU
            if modrm == 0xe3:
                e('R.top = 0; R.tag = 0; R.c0 = R.c1 = R.c2 = R.c3 = 0; __asm__ volatile("fninit" ::: "memory");'); return
            raise LiftError('x87 db %02x (fcmov/fcomi)' % modrm)
        if op == 0xdd:
            if reg == 0: e('R.tag &= ~(1u << ((R.top + %d) & 7));' % i); return
            if reg == 2: e('ST(%d) = ST(0);' % i); return
            if reg == 3: e('ST(%d) = ST(0); fpop();' % i); return
            if reg in (4, 5):
                e('fcmp(ST(0), ST(%d));' % i)
                if reg == 5: e('fpop();')
                return
            raise LiftError('x87 dd %02x' % modrm)
        if op == 0xdf:
            if modrm == 0xe0:
                e('eax = (eax & 0xffff0000u) | fsw();'); return
            raise LiftError('x87 df %02x (fcomip)' % modrm)
        raise LiftError('x87 %02x %02x' % (op, modrm))

def flag_check(f):
    """Each flag read must see a flag write inside this function on every path."""
    reads, writes = {}, {}
    for a, ins in f.insns.items():
        m = ins.mnemonic.split()[-1]
        r = w = ''
        if m.startswith('j') and m not in ('jmp', 'jecxz', 'jcxz'):
            r = CCFLAGS.get(m[1:], ALLF)
        elif m.startswith('set'):
            r = CCFLAGS.get(m[3:], ALLF)
        elif m in ('adc', 'sbb', 'rcl', 'rcr', 'cmc'):
            r = 'c'
        elif m in ('lahf', 'pushfd', 'pushf'):
            r = 'czsp'
        if m in ('add', 'sub', 'cmp', 'adc', 'sbb', 'and', 'or', 'xor', 'test', 'neg', 'imul', 'mul', 'scasb', 'scasw', 'scasd', 'cmpsb', 'cmpsw', 'cmpsd', 'xadd', 'cmpxchg', 'shld', 'shrd', 'sahf', 'popfd', 'popf'):
            w = ALLF
        elif m in ('inc', 'dec'):
            w = 'zsop'
        elif m in ('shl', 'sal', 'shr', 'sar'):
            ops = ins.operands
            if len(ops) == 1 or (ops[1].type == X86_OP_IMM and ops[1].imm & 31):
                w = ALLF
        elif m in ('rol', 'ror', 'bt', 'bts', 'btr', 'btc', 'clc', 'stc', 'cmc'):
            w = 'c' + ('o' if m in ('rol', 'ror') else '')
        elif m in ('bsf', 'bsr'):
            w = 'z'
        if m == 'call':
            w = '!'
            if ins.operands[0].type == X86_OP_IMM and (ins.operands[0].imm & 0xffffffff) in FLAGRET:
                w = ALLF
        reads[a], writes[a] = r, w
    # forward data flow: set of flags defined on all paths
    preds = {}
    for a, ss in f.succ.items():
        for s in ss:
            preds.setdefault(s, []).append(a)
    full = set(ALLF)
    IN = {a: set(full) for a in f.insns}
    IN[f.entry] = set()
    OUT = {}
    def tf(a, s):
        w = writes[a]
        if w == '!': return set()
        return s | set(w)
    changed = True
    order = sorted(f.insns)
    while changed:
        changed = False
        for a in order:
            if a == f.entry: inn = set()
            else:
                ps = [p for p in preds.get(a, []) if p in f.insns]
                inn = set(full)
                for p in ps: inn &= OUT.get(p, set(full))
                if not ps: inn = set()
            o = tf(a, inn)
            if IN[a] != inn or OUT.get(a) != o:
                IN[a], OUT[a] = inn, o
                changed = True
    bad = []
    for a in order:
        need = set(reads[a]) - IN[a]
        if need:
            bad.append('%08x %s reads flags %s set by a callee or the caller' % (a, f.insns[a].mnemonic, ''.join(sorted(need))))
    return bad

def lift(img, md, entry, entries):
    f = Func(img, md, entry, entries)
    f.discover()
    fb = flag_check(f)
    if fb:
        raise LiftError(fb[0])
    em = Emitter(f)
    lines = ['#include "rc.h"', 'void f_%08x(void)' % entry, '{', '    LOADREGS;', '    goto L_%08x;' % entry]
    for a in sorted(set(f.insns) | set(f.bad)):
        if a in f.targets or a == entry:
            lines.append('L_%08x: ;' % a)
        if a in f.bad:
            lines.append('    rc_unreachable(0x%08xu); /* %s */' % (a, f.bad[a]))
            continue
        ins = f.insns[a]
        em.out = []
        try:
            em.emit(ins)
        except LiftError as ex:
            raise LiftError('%08x %s %s: %s' % (a, ins.mnemonic, ins.op_str, ex))
        lines.append('    /* %08x %s %s */' % (a, ins.mnemonic, ins.op_str))
        lines.extend(em.out)
        if em.uses_mmx and 'u64 mm0' not in lines[3]:
            lines[3] += ' u64 mm0 = 0, mm1 = 0, mm2 = 0, mm3 = 0, mm4 = 0, mm5 = 0, mm6 = 0, mm7 = 0;'
        # an instruction that does not continue to the next one (no-return call, trap)
        nxt = a + ins.size
        m = ins.mnemonic
        if m not in ('jmp', 'ret', 'int3', 'hlt', 'ud2') and nxt not in f.succ.get(a, []):
            lines.append('    rc_unreachable(0x%08xu);' % nxt)
    lines.append('}')
    # check: every fall-through goes to the next emitted address
    addrs = sorted(set(f.insns) | set(f.bad))
    for k, a in enumerate(addrs):
        if a in f.bad: continue
        ins = f.insns[a]
        m = ins.mnemonic
        nxt = a + ins.size
        falls = m not in ('jmp', 'ret', 'int3', 'hlt', 'ud2') and nxt in f.succ.get(a, [])
        if falls and (k + 1 >= len(addrs) or addrs[k + 1] != nxt):
            raise LiftError('overlapping instructions near %08x' % a)
    return '\n'.join(lines) + '\n', f

def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    img = Image(EXE)
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    md.detail = True
    entries = set(int(x, 16) for x in open(ENTRIES).read().split())
    for x in sys.argv[2:]:
        a = int(x, 16)
        t0 = time.time()
        try:
            src, f = lift(img, md, a, entries | {a})
            open(os.path.join(out, 'f_%08x.c' % a), 'w').write(src)
            print(json.dumps({'addr': '%08x' % a, 'ok': True, 'insns': len(f.insns), 'calls': sorted('%08x' % c for c in f.calls),
                              'tables': len(f.tables), 'notes': f.notes, 'sec': round(time.time() - t0, 4)}), flush=True)
        except LiftError as ex:
            print(json.dumps({'addr': '%08x' % a, 'ok': False, 'error': str(ex), 'sec': round(time.time() - t0, 4)}), flush=True)

if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""import_sites.py: list every game.exe use of the USER32, GDI32, COMCTL32 and IMM32 imports.
usage: import_sites.py [out.txt]
A use is an instruction whose memory operand or immediate is an import slot:
  call [slot], jmp [slot], mov reg,[slot] (the register is then called), push [slot].
Each function from all-addresses.txt is disassembled from its start to the next start.
For "mov reg,[slot]", the later "call reg" in the same function are counted as calls.
Output: one summary line per import (name, sites, calls), then one line per site."""
import os, sys, struct, bisect, collections
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '../../../tools'))
import lift as L
from capstone import Cs, CS_ARCH_X86, CS_MODE_32
from capstone.x86 import X86_OP_MEM, X86_OP_IMM, X86_OP_REG

WANT = {'USER32.DLL', 'GDI32.DLL', 'COMCTL32.DLL', 'IMM32.DLL'}
img = L.Image(L.EXE); d = img.d
pe = struct.unpack_from('<I', d, 0x3c)[0]
rva = struct.unpack_from('<I', d, pe + 24 + 96 + 8)[0]
def cstr(va):
    b = img.read(va, 256); return b[:b.index(b'\0')].decode()
slots = {}; order = []
p = img.base + rva
while True:
    olt, ts, fc, name, iat = struct.unpack('<IIIII', img.read(p, 20))
    if not name: break
    dll = cstr(img.base + name).upper(); t = img.base + (olt or iat); s = img.base + iat
    while True:
        e = img.u32(t)
        if not e: break
        fn = ('#%d' % (e & 0xffff)) if e & 0x80000000 else cstr(img.base + e + 2)
        if dll in WANT: slots[s] = (dll, fn); order.append((dll, fn, s))
        t += 4; s += 4
    p += 20
starts = sorted(set(int(x.split()[0], 16) for x in open(L.ENTRIES) if x.strip() and not x.startswith('#')))
starts = [a for a in starts if img.text_lo <= a < img.text_hi]
md = Cs(CS_ARCH_X86, CS_MODE_32); md.detail = True
sites = collections.defaultdict(list)   # (dll, fn) -> [(addr, func, kind)]
direct = []   # (addr, func, target) of every direct call/jmp, to find calls through import thunks
thunks = {}   # thunk address ("jmp [slot]") -> slot
for i, a in enumerate(starts):
    end = starts[i + 1] if i + 1 < len(starts) else img.text_hi
    end = min(end, a + 0x10000)
    code = img.read(a, end - a)
    held = {}   # register -> slot
    for ins in md.disasm(code, a):
        hit = None
        for op in ins.operands:
            if op.type == X86_OP_MEM and op.mem.base == 0 and op.mem.index == 0 and op.mem.disp & 0xffffffff in slots:
                hit = op.mem.disp & 0xffffffff
            elif op.type == X86_OP_IMM and op.imm & 0xffffffff in slots:
                hit = op.imm & 0xffffffff
        m = ins.mnemonic
        if hit is not None:
            kind = m
            if m == 'mov' and ins.operands[0].type == X86_OP_REG:
                held[ins.operands[0].reg] = hit; kind = 'load'
            sites[slots[hit]].append((ins.address, a, kind))
            if m == 'jmp': thunks[ins.address] = hit
            continue
        if m in ('call', 'jmp') and ins.operands and ins.operands[0].type == X86_OP_IMM:
            direct.append((ins.address, a, ins.operands[0].imm & 0xffffffff))
        if m == 'call' and ins.operands and ins.operands[0].type == X86_OP_REG and ins.operands[0].reg in held:
            sites[slots[held[ins.operands[0].reg]]].append((ins.address, a, 'call-reg'))
            continue
        # a write to a held register ends the hold
        if ins.operands and ins.operands[0].type == X86_OP_REG and m not in ('cmp', 'test', 'push', 'call'):
            held.pop(ins.operands[0].reg, None)
        if m in ('ret', 'jmp') and not ins.operands or m == 'ret':
            pass
for addr, func, tgt in direct:
    if tgt in thunks: sites[slots[thunks[tgt]]].append((addr, func, 'call-thunk'))
out = open(sys.argv[1] if len(sys.argv) > 1 else 'import-sites.txt', 'w')
out.write('# dll function slot sites calls  (calls = call [slot] + call reg after load + call to the jmp [slot] thunk)\n')
for dll, fn, s in order:
    v = sites[(dll, fn)]
    calls = sum(1 for x in v if x[2] in ('call', 'call-reg', 'call-thunk'))
    out.write('%-12s %-28s %08x sites=%d calls=%d\n' % (dll, fn, s, len(v), calls))
out.write('\n# site function kind dll function\n')
for dll, fn, s in order:
    for addr, func, kind in sorted(sites[(dll, fn)]):
        out.write('%08x %08x %-8s %s %s\n' % (addr, func, kind, dll, fn))
print('imports:', len(order), 'with calls:', sum(1 for dll, fn, s in order if any(x[2] != 'load' for x in sites[(dll, fn)])))

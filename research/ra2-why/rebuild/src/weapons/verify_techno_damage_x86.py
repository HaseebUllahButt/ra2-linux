#!/usr/bin/env python3
"""Compare the Techno damage prefix with the owned base executable.

Build: g++ -std=c++17 -O2 -Wall -Wextra -Werror -I rebuild/src \
  rebuild/src/weapons/techno_damage{,_test}.cpp -o /tmp/ra2_techno_damage
Run: RA2_GAME_DIR=/path/to/game python3 rebuild/src/weapons/verify_techno_damage_x86.py /tmp/ra2_techno_damage
"""
import os
from pathlib import Path
import random
import struct
import subprocess
import sys
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import *

u = Uc(UC_ARCH_X86, UC_MODE_32)
u.mem_map(0x400000, 0x800000)
u.mem_write(0x400000, (Path(os.environ['RA2_GAME_DIR']) / 'game.exe').read_bytes()[:0x431000])
u.mem_map(0x1000000, 0x100000)
u.mem_map(0x2000000, 0x100000)
u.mem_map(0x3000000, 0x1000)
base, stack, stop = 0x1000000, 0x20fff00, 0x3000000
obj, attacker, vtable, kind, otherkind, rules, damageptr, warhead, divisor = (
    base + n for n in (0x1000, 0x3000, 0x5000, 0x7000, 0x9000, 0xb000, 0xd000, 0xf000, 0x11000))

def w(p, v): u.mem_write(p, struct.pack('<I', v & 0xffffffff))
def read(p): return struct.unpack('<i', u.mem_read(p, 4))[0]
def ret(cpu, value, count=0):
    sp = cpu.reg_read(UC_X86_REG_ESP)
    cpu.reg_write(UC_X86_REG_EAX, value & 0xffffffff)
    cpu.reg_write(UC_X86_REG_EIP, read(sp) & 0xffffffff)
    cpu.reg_write(UC_X86_REG_ESP, sp + 4 + 4 * count)

# House armor is a resolved x87 value. This stub returns it in ST0.
u.mem_write(stop+0x200, b'\xdd\x05' + struct.pack('<I', divisor) + b'\xc2\x04\x00')
w(obj, vtable); w(attacker, vtable); w(0x839848, rules)
for off, addr in ((0x80, 0x100), (0x32c, 0x110), (0x1bc, 0x120)):
    w(vtable+off, stop+addr)
result = None
case = None

def hook(cpu, address, size, user):
    global result
    if address == stop+0x100:
        ret(cpu, kind if cpu.reg_read(UC_X86_REG_ECX) == obj or case[11] else otherkind)
    elif address == stop+0x110: ret(cpu, case[13])
    elif address == stop+0x120: ret(cpu, case[14])
    elif address == 0x4f58a0: cpu.reg_write(UC_X86_REG_EIP, stop+0x200)
    elif address == 0x7133a0: ret(cpu, case[4])
    elif address == 0x713420: ret(cpu, case[5])
    elif address == 0x6cdd6c: result = (1, read(damageptr)); cpu.emu_stop()
    elif address == 0x47f600: result = (2, read(damageptr)); cpu.emu_stop()
    elif address == 0x6cddee: result = (3, 0); cpu.emu_stop()
    elif address == 0x6cde17: result = (4, read(damageptr)); cpu.emu_stop()
    elif address in (0x6cdeb8, 0x6cdeeb): result = (5, 0); cpu.emu_stop()
    elif address == 0x5d3d20: result = (0, read(damageptr)); cpu.emu_stop()
for addr in (stop+0x100, stop+0x110, stop+0x120, 0x4f58a0, 0x7133a0, 0x713420,
             0x6cdd6c, 0x47f600, 0x6cddee, 0x6cde17, 0x6cdeb8, 0x6cdeeb, 0x5d3d20):
    u.hook_add(UC_HOOK_CODE, hook, begin=addr, end=addr)

rng = random.Random(0x6cdc40)
cases=[]
for n in range(5000):
    raw=rng.choice((-300,-1,0,1,45,90,300,1000))
    bools=[rng.randrange(2) for _ in range(17)]
    cases.append((raw, bools[0], rng.choice((.25,.5,1.,1.5,2.,3.)),
                  rng.choice((.25,.5,1.,1.5,2.,3.)), *bools[1:5],
                  rng.choice((.25,.5,1.,1.5,2.,3.)), *bools[5:]))
lines=subprocess.check_output([sys.argv[1]], input=''.join(' '.join(map(str,c))+'\n' for c in cases), text=True).splitlines()
counts={}
for case,line in zip(cases,lines):
    (raw,bypass,house,armor,veteran,elite,veteran_armor,elite_armor,veteran_mult,
     has_attacker,suppress,same_type,same_house,invulnerable,immune,side,has_warhead,
     flag163,flag164,flagafe,flagafd)=case
    w(damageptr, raw); w(stack, stop); w(stack+4, damageptr); w(stack+8, 0)
    w(stack+12, warhead if has_warhead else 0); w(stack+16, attacker if has_attacker else 0)
    w(stack+20, bypass); w(stack+24, 0)
    w(obj+0x1b4, base+0x13000); w(attacker+0x1b4, base+(0x13000 if same_house else 0x14000))
    u.mem_write(divisor, struct.pack('<d',house)); u.mem_write(obj+0x120,struct.pack('<d',armor))
    u.mem_write(rules+0x5a8,struct.pack('<d',veteran_mult))
    for addr,val in ((kind+0x26d,veteran_armor),(kind+0x27f,elite_armor),
                     (kind+0xa98,suppress),(kind+0x5d0,side),(kind+0xafe,flagafe),
                     (kind+0xafd,flagafd),(warhead+0x163,flag163),(warhead+0x164,flag164)):
        u.mem_write(addr,bytes((val,)))
    u.reg_write(UC_X86_REG_ECX,obj); u.reg_write(UC_X86_REG_ESP,stack); u.reg_write(UC_X86_REG_FPCW,0xe7f)
    result=None
    u.emu_start(0x6cdc40,stop,count=10000)
    expected=tuple(map(int,line.split()))
    assert result == expected, (case,result,expected)
    counts[result[0]]=counts.get(result[0],0)+1
assert len(lines)==len(cases) and set(counts)==set(range(6)), counts
print(f'Base game.exe: {len(cases)} Techno damage prefixes match; stages {counts}')

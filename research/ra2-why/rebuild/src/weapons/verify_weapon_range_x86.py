#!/usr/bin/env python3
"""Compare resolved WeaponType range conversion and defaults with base game.exe.

Build: g++ -std=c++17 -O2 -Wall -Wextra -Werror -I rebuild/src \
  rebuild/src/weapons/{cannon_range,weapon_range_test,weapons}.cpp \
  rebuild/src/sim/{combat,target,distance}.cpp -o rebuild/build/weapon_range_test
Run: RA2_GAME_DIR=/path/to/game python3 rebuild/src/weapons/verify_weapon_range_x86.py rebuild/build/weapon_range_test
"""
import os
from pathlib import Path
import random
import struct
import subprocess
import sys
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE
from unicorn.x86_const import *
u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(0x400000,0x800000)
u.mem_write(0x400000,(Path(os.environ['RA2_GAME_DIR'])/'game.exe').read_bytes()[:0x431000])
u.mem_map(0x1000000,0x100000);u.mem_map(0x2000000,0x100000);u.mem_map(0x3000000,4096)
base,stack,stop=0x1000000,0x20fff00,0x3000000
obj,value=base+0x1000,base+0x5000
def w(p,n):u.mem_write(p,struct.pack('<I',n&0xffffffff))
def read(p):return struct.unpack('<I',u.mem_read(p,4))[0]
def ret(cpu,val,n=0):
 sp=cpu.reg_read(UC_X86_REG_ESP);cpu.reg_write(UC_X86_REG_EAX,val);cpu.reg_write(UC_X86_REG_EIP,read(sp));cpu.reg_write(UC_X86_REG_ESP,sp+4+n*4)
# Range INI reader is resolved to a double in this comparison.
u.mem_write(stop+0x200,b'\xdd\x05'+struct.pack('<I',value)+b'\xc2\x10\x00')
def hook(cpu,addr,size,user):
 if addr==0x50e3a0:cpu.reg_write(UC_X86_REG_EIP,stop+0x200)
 else:ret(cpu,0,2)
for addr in (0x50e3a0,0x46d020,0x50b860):u.hook_add(UC_HOOK_CODE,hook,begin=addr,end=addr)
# Execute the actual WeaponType constructor writes after its parent call.
u.reg_write(UC_X86_REG_ESI,obj);u.reg_write(UC_X86_REG_ESP,stack);u.emu_start(0x732eaf,0x733046,count=10000)
assert read(obj+0x9c)==1 and u.mem_read(obj+0x10f,1)==b'\1'
for off in (0x105,0x106,0x109,0x10c,0x10d,0x125):assert u.mem_read(obj+off,1)==b'\0'
rng=random.Random(0x469ae0)
cases=[(-1.,rng.randrange(-10000,10000)),(5.75,0),(0.,91)]
for _ in range(3000):cases.append((rng.randrange(-1000000,1000000)/128.,rng.randrange(-10000,10000)))
lines=subprocess.check_output([sys.argv[1]],input=''.join(f'{n} {fb}\n' for n,fb in cases),text=True).splitlines()
for (n,fb),line in zip(cases,lines):
 u.mem_write(value,struct.pack('<d',n));w(stack,stop);w(stack+4,0);w(stack+8,0);w(stack+12,fb)
 u.reg_write(UC_X86_REG_ESP,stack);u.reg_write(UC_X86_REG_FPCW,0xe7f);u.emu_start(0x469ae0,stop,count=10000)
 actual=readvalue=u.reg_read(UC_X86_REG_EAX);actual=actual if actual<0x80000000 else actual-0x100000000
 assert actual==int(line),((n,fb),actual,line)
assert len(lines)==len(cases)
print(f'Base game.exe: weapon defaults and {len(cases)} range conversions match')

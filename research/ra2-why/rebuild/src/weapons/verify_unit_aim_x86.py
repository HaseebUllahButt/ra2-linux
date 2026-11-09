#!/usr/bin/env python3
"""Compare stationary Unit aim using its real vtable functions with base game.exe.

Build: g++ -std=c++17 -O2 -Wall -Wextra -Werror -I rebuild/src \
  rebuild/src/weapons/unit_aim_test.cpp rebuild/src/sim/damage_runtime.cpp \
  rebuild/src/weapons/weapons.cpp rebuild/src/sim/{combat,target,distance}.cpp -o rebuild/build/unit_aim_test
Run: RA2_GAME_DIR=/path/to/game python3 rebuild/src/weapons/verify_unit_aim_x86.py rebuild/build/unit_aim_test
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
source,target,vt,loc,locvt,out=(base+n for n in (0x1000,0x3000,0x5000,0x7000,0x9000,0xb000))
def w(p,n):u.mem_write(p,struct.pack('<I',n&0xffffffff))
def read(p):return struct.unpack('<I',u.mem_read(p,4))[0]
def ret(cpu,v,n=0):
 sp=cpu.reg_read(UC_X86_REG_ESP);cpu.reg_write(UC_X86_REG_EAX,v);cpu.reg_write(UC_X86_REG_EIP,read(sp));cpu.reg_write(UC_X86_REG_ESP,sp+4+n*4)
def hook(cpu,addr,size,user):ret(cpu,1 if addr==stop+0x100 else 0,0 if addr==stop+0x100 else 1)
for addr in (stop+0x100,stop+0x110):u.hook_add(UC_HOOK_CODE,hook,begin=addr,end=addr)
w(source+0x228,target);w(target,vt);w(vt+0x2c,stop+0x100);w(vt+0x58,0x4102e0);w(vt+0x48,0x5d4e40)
w(target+0x564,loc);w(loc,locvt);w(locvt+0x10,stop+0x110)
rng=random.Random(0x6d65b0);cases=[tuple(rng.randrange(-1000000,1000001) for _ in range(3)) for i in range(2000)]
lines=subprocess.check_output([sys.argv[1]],input=''.join(' '.join(map(str,c))+'\n' for c in cases),text=True).splitlines()
for pos,line in zip(cases,lines):
 u.mem_write(target+0x88,struct.pack('<iii',*pos));w(stack,stop);w(stack+4,out)
 u.reg_write(UC_X86_REG_ECX,source);u.reg_write(UC_X86_REG_ESP,stack);u.emu_start(0x6d65b0,stop,count=10000)
 actual=struct.unpack('<iii',u.mem_read(out,12));assert actual==tuple(map(int,line.split())),(pos,actual,line)
assert len(lines)==len(cases)
print(f'Base game.exe: {len(cases)} stationary Unit aim points match')

#!/usr/bin/env python3
"""Compare single-shot Unit delay and full RNG state with base game.exe.

Build: g++ -std=c++17 -O2 -Wall -Wextra -Werror -I rebuild/src \
  rebuild/src/weapons/{fire_timer,fire_delay_test}.cpp rebuild/src/sim/random.cpp -o /tmp/ra2_fire_delay
Run: RA2_GAME_DIR=/path/to/game python3 rebuild/src/weapons/verify_fire_delay_x86.py /tmp/ra2_fire_delay
"""
import os
from pathlib import Path
import random
import struct
import subprocess
import sys
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE
from unicorn.x86_const import *
u=Uc(UC_ARCH_X86,UC_MODE_32)
u.mem_map(0x400000,0x800000);u.mem_write(0x400000,(Path(os.environ['RA2_GAME_DIR'])/'game.exe').read_bytes()[:0x431000])
u.mem_map(0x1000000,0x100000);u.mem_map(0x2000000,0x100000);u.mem_map(0x3000000,4096)
base,stack,stop=0x1000000,0x20fff00,0x3000000
actor,vtable,kind,slot,weapon,house,scenario,rules=(base+n for n in (0x1000,0x3000,0x5000,0x7000,0x9000,0xb000,0xd000,0xf000))
def w(p,n):u.mem_write(p,struct.pack('<I',n&0xffffffff))
def read(p):return struct.unpack('<I',u.mem_read(p,4))[0]
def ret(cpu,v,n=0):
 sp=cpu.reg_read(UC_X86_REG_ESP);cpu.reg_write(UC_X86_REG_EAX,v);cpu.reg_write(UC_X86_REG_EIP,read(sp));cpu.reg_write(UC_X86_REG_ESP,sp+4+n*4)
case=None
def hook(cpu,addr,size,user):
 if addr==stop+0x100:ret(cpu,1)
 elif addr==stop+0x110:ret(cpu,slot,1)
 elif addr==stop+0x120:ret(cpu,kind)
 elif addr==0x7133a0:ret(cpu,case[4])
 elif addr==0x713420:ret(cpu,case[5])
for addr in (stop+0x100,stop+0x110,stop+0x120,0x7133a0,0x713420):u.hook_add(UC_HOOK_CODE,hook,begin=addr,end=addr)
w(actor,vtable);w(vtable+0x2c,stop+0x100);w(vtable+0x39c,stop+0x110);w(vtable+0x80,stop+0x120)
w(actor+0x5ac,kind);w(actor+0x1b4,house);w(actor+0x310,1);w(slot,weapon);w(weapon+0x9c,1)
w(0xa3d290,scenario);w(0x839848,rules)
rng=random.Random(0x6c9cfe);cases=[]
for _ in range(2000):
 cases.append((rng.randrange(2**32),rng.randrange(12),rng.choice((0,1,15,65,100,1000)),
               rng.choice((.25,.5,1.,1.2,1.5,2.)),*(rng.randrange(2) for i in range(4)),
               rng.choice((.25,.5,.9,1.,1.5,2.))))
lines=subprocess.check_output([sys.argv[1]],input=''.join(' '.join(map(str,c))+'\n' for c in cases),text=True).splitlines()
for case,line in zip(cases,lines):
 seed,warmup,rof,mult,veteran,elite,vflag,eflag,vetmult=case
 w(stack,stop);w(stack+4,seed);u.reg_write(UC_X86_REG_ECX,scenario+0x218);u.reg_write(UC_X86_REG_ESP,stack);u.emu_start(0x638790,stop,count=100000)
 for _ in range(warmup):
  w(stack,stop);u.reg_write(UC_X86_REG_ECX,scenario+0x218);u.reg_write(UC_X86_REG_ESP,stack);u.emu_start(0x638840,stop,count=1000)
 w(weapon+0xb0,rof);u.mem_write(house+0xf0,struct.pack('<d',mult));u.mem_write(rules+0x5b0,struct.pack('<d',vetmult))
 u.mem_write(kind+0x270,bytes((vflag,)));u.mem_write(kind+0x282,bytes((eflag,)))
 w(stack,stop);w(stack+4,0);u.reg_write(UC_X86_REG_ECX,actor);u.reg_write(UC_X86_REG_ESP,stack);u.reg_write(UC_X86_REG_FPCW,0xe7f)
 u.emu_start(0x6c9c00,stop,count=10000)
 delay=u.reg_read(UC_X86_REG_EAX);delay=delay if delay<0x80000000 else delay-0x100000000
 state=scenario+0x218;words=struct.unpack('<250I',u.mem_read(state+12,1000))
 actual=(delay,read(state+4),read(state+8),*words);expected=tuple(map(int,line.split()))
 assert actual==expected,(case,actual,expected)
assert len(lines)==len(cases)
print(f'Base game.exe: {len(cases)} reload delays and full RNG states match')

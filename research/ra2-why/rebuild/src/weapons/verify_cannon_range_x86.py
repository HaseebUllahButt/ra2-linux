#!/usr/bin/env python3
"""Compare guarded Cannon range checks with the owned base game.exe.

Build: g++ -std=c++17 -O2 -Wall -Wextra -Werror -I rebuild/src \
  rebuild/src/weapons/{cannon_range,cannon_range_test,weapons}.cpp \
  rebuild/src/sim/{combat,target,distance}.cpp -o rebuild/build/cannon_range_test
Run: RA2_GAME_DIR=/path/to/game python3 rebuild/src/weapons/verify_cannon_range_x86.py rebuild/build/cannon_range_test
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
actor,target_obj,vtable,weapon,projectile,house,cell,rules,position=(base+n for n in (0x1000,0x3000,0x5000,0x7000,0x9000,0xb000,0xd000,0xf000,0x11000))
def w(p,n):u.mem_write(p,struct.pack('<I',n&0xffffffff))
def read(p):return struct.unpack('<I',u.mem_read(p,4))[0]
def ret(cpu,v,n=0):
 sp=cpu.reg_read(UC_X86_REG_ESP);cpu.reg_write(UC_X86_REG_EAX,v&0xffffffff);cpu.reg_write(UC_X86_REG_EIP,read(sp));cpu.reg_write(UC_X86_REG_ESP,sp+4+n*4)
case=None
def hook(cpu,addr,size,user):
 if addr==stop+0x100:ret(cpu,0)
 elif addr==stop+0x110:
  ptr=read(cpu.reg_read(UC_X86_REG_ESP)+4);cpu.mem_write(ptr,struct.pack('<iii',*case[3:6]));ret(cpu,ptr,1)
 elif addr==0x548000:ret(cpu,cell,1)
 elif addr==0x4bbaa0:ret(cpu,case[9],2)
for addr in (stop+0x100,stop+0x110,0x548000,0x4bbaa0):u.hook_add(UC_HOOK_CODE,hook,begin=addr,end=addr)
w(actor,vtable);w(target_obj,vtable);w(vtable+0x54,stop+0x100);w(vtable+0x48,stop+0x110)
w(actor+0x1b4,house);w(weapon+0xa0,projectile);u.mem_write(projectile+0x26b,b'\1');w(0x839848,rules);w(cell+0x140,0)
rng=random.Random(0x6c4bb0);cases=[]
for _ in range(5000):
 origin=(rng.randrange(-1000,1001),rng.randrange(-1000,1001),rng.randrange(-100,101))
 target=(origin[0]+rng.randrange(-4500,4501),origin[1]+rng.randrange(-4500,4501),origin[2]+rng.randrange(-500,501))
 cases.append((*origin,*target,rng.choice((128,512,1024,1536,2048,4096)),rng.choice((0,0,128,512)),rng.choice((1,3,6,9)),rng.randrange(2)))
# Include exact range and vertical cases.
for d in (0,1,255,256,511,512,513,1023,1024,1025,1536):
 for z in (-100,0,100):cases.append((0,0,0,d,0,z,1024,0,6,0))
lines=subprocess.check_output([sys.argv[1]],input=''.join(' '.join(map(str,c))+'\n' for c in cases),text=True).splitlines();counts={}
for case,line in zip(cases,lines):
 x,y,z,tx,ty,tz,range_,minimum,gravity,blocked=case
 u.mem_write(position,struct.pack('<iii',x,y,z));w(weapon+0xb4,range_);w(weapon+0xb8,minimum);w(rules+0x1320,gravity)
 w(stack,stop);w(stack+4,position);w(stack+8,target_obj);w(stack+12,weapon)
 u.reg_write(UC_X86_REG_ECX,actor);u.reg_write(UC_X86_REG_ESP,stack);u.reg_write(UC_X86_REG_FPCW,0xe7f)
 u.emu_start(0x6c4bb0,stop,count=100000)
 actual=u.reg_read(UC_X86_REG_EAX)&255;expected=int(line)
 assert actual==expected,(case,actual,expected)
 counts[actual]=counts.get(actual,0)+1
assert len(lines)==len(cases) and set(counts)=={0,1},counts
print(f'Base game.exe: {len(cases)} guarded Cannon range checks match; results {counts}')

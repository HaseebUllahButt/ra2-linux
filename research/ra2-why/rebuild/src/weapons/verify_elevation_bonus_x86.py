#!/usr/bin/env python3
"""Compare Cannon arcing elevation bonus with base game.exe.

Build: g++ -std=c++17 -O2 -Wall -Wextra -Werror -I rebuild/src \
  rebuild/src/weapons/{cannon_range,elevation_bonus_test,weapons}.cpp \
  rebuild/src/sim/{combat,target,distance}.cpp -o rebuild/build/elevation_bonus_test
Run: RA2_GAME_DIR=/path/to/game python3 rebuild/src/weapons/verify_elevation_bonus_x86.py rebuild/build/elevation_bonus_test
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
actor,target,vt,sourcecell,targetcell,rules=(base+n for n in (0x1000,0x3000,0x5000,0x7000,0x9000,0xb000))
def w(p,n):u.mem_write(p,struct.pack('<I',n&0xffffffff))
def read(p):return struct.unpack('<I',u.mem_read(p,4))[0]
def ret(cpu,v,n=0):
 sp=cpu.reg_read(UC_X86_REG_ESP);cpu.reg_write(UC_X86_REG_EAX,v&0xffffffff);cpu.reg_write(UC_X86_REG_EIP,read(sp));cpu.reg_write(UC_X86_REG_ESP,sp+4+n*4)
case=None
def hook(cpu,addr,size,user):
 ecx=cpu.reg_read(UC_X86_REG_ECX)
 if addr==stop+0x100:ret(cpu,case[5] if ecx==actor else case[6])
 elif addr==stop+0x110:
  ptr=read(cpu.reg_read(UC_X86_REG_ESP)+4);cpu.mem_write(ptr,struct.pack('<iii',*(0,0,0) if ecx==actor else (512,512,0)));ret(cpu,ptr,1)
 elif addr==0x548070:
  ptr=read(cpu.reg_read(UC_X86_REG_ESP)+4);x,y=struct.unpack('<hh',cpu.mem_read(ptr,4));ret(cpu,sourcecell if (x,y)==(0,0) else targetcell,1)
for addr in (stop+0x100,stop+0x110,0x548070):u.hook_add(UC_HOOK_CODE,hook,begin=addr,end=addr)
w(actor,vt);w(target,vt);w(vt+0x50,stop+0x100);w(vt+0x48,stop+0x110);w(0x839848,rules)
rng=random.Random(0x6c4a70);cases=[]
for _ in range(3000):cases.append((rng.randrange(-10,11),rng.randrange(-10,11),rng.choice((1,2,3,4,5)),rng.choice((.25,.5,1.,1.5,2.)),rng.choice((0.,1.,2.5,5.,10.)),rng.randrange(2),rng.randrange(2)))
lines=subprocess.check_output([sys.argv[1]],input=''.join(' '.join(map(str,c))+'\n' for c in cases),text=True).splitlines()
for case,line in zip(cases,lines):
 source,dest,div,factor,cap,a,b=case
 u.mem_write(sourcecell+0x11b,bytes((source&255,)));u.mem_write(targetcell+0x11b,bytes((dest&255,)))
 w(sourcecell+0x140,0);w(targetcell+0x140,0);w(rules+0x1480,div)
 u.mem_write(rules+0x1488,struct.pack('<d',factor));u.mem_write(rules+0x1490,struct.pack('<d',cap))
 w(stack,stop);w(stack+4,target);u.reg_write(UC_X86_REG_ECX,actor);u.reg_write(UC_X86_REG_ESP,stack);u.reg_write(UC_X86_REG_FPCW,0xe7f)
 u.emu_start(0x6c4a70,stop,count=100000);actual=u.reg_read(UC_X86_REG_EAX)
 assert actual==int(line),(case,actual,line)
assert len(lines)==len(cases)
print(f'Base game.exe: {len(cases)} arcing elevation bonuses match')

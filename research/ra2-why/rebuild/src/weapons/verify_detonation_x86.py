#!/usr/bin/env python3
"""Compare ordinary Unit impact adjustment and post-impact RNG with game.exe.

Build: g++ -std=c++17 -O2 -Wall -Wextra -Werror -I rebuild/src \
  rebuild/src/weapons/{detonation,detonation_test}.cpp rebuild/src/sim/{random,distance}.cpp -o rebuild/build/detonation_test
Run: RA2_GAME_DIR=/path/to/game python3 rebuild/src/weapons/verify_detonation_x86.py rebuild/build/detonation_test
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
projectile,target,vt,tvt,kind,warhead,scenario=(base+n for n in (0x1000,0x3000,0x5000,0x7000,0x9000,0xb000,0xd000))
def w(p,n):u.mem_write(p,struct.pack('<I',n&0xffffffff))
def read(p):return struct.unpack('<I',u.mem_read(p,4))[0]
def ret(cpu,v,n=0):
 sp=cpu.reg_read(UC_X86_REG_ESP);cpu.reg_write(UC_X86_REG_EAX,v);cpu.reg_write(UC_X86_REG_EIP,read(sp));cpu.reg_write(UC_X86_REG_ESP,sp+4+n*4)
observed=None

def hook(cpu,addr,size,user):
 global observed
 if addr==stop+0x100:ret(cpu,0)
 elif addr==stop+0x110:ret(cpu,1)
 elif addr==0x462110:
  ptr=read(cpu.reg_read(UC_X86_REG_ESP)+4);observed=struct.unpack('<iii',cpu.mem_read(ptr,12));cpu.emu_stop()
 elif addr in (0x462103,0x4620e4):cpu.emu_stop()
for addr in (stop+0x100,stop+0x110,0x462110,0x462103,0x4620e4):u.hook_add(UC_HOOK_CODE,hook,begin=addr,end=addr)
w(projectile,vt);w(vt+0x48,0x5d4e40);w(target,tvt);w(tvt+0x48,0x5d4e40);w(tvt+0x58,0x4102e0)
w(tvt+0x54,stop+0x100);w(tvt+0x2c,stop+0x110);w(projectile+0x98,kind);w(projectile+0x110,warhead)
u.mem_write(kind+0x26b,b'\1');w(kind+0x27c,1);w(0xa3d290,scenario)
rng=random.Random(0x461de0);cases=[]
for _ in range(3000):
 pos=tuple(rng.randrange(-1000,1001) for i in range(3));d=tuple(rng.choice((-85,-43,-42,-41,-32,-31,-1,0,1,31,32,41,42,43,85)) for i in range(3))
 cases.append((*pos,rng.randrange(2),*(pos[i]+d[i] for i in range(3))))
lines=subprocess.check_output([sys.argv[1],'--position'],input=''.join(' '.join(map(str,c))+'\n' for c in cases),text=True,env=os.environ).splitlines()
for case,line in zip(cases,lines):
 x,y,z,present,tx,ty,tz=case
 u.mem_write(projectile+0x88,struct.pack('<iii',x,y,z));u.mem_write(target+0x88,struct.pack('<iii',tx,ty,tz));w(projectile+0xf4,target if present else 0)
 w(stack,stop);w(stack+4,0);u.reg_write(UC_X86_REG_ECX,projectile);u.reg_write(UC_X86_REG_ESP,stack);u.reg_write(UC_X86_REG_FPCW,0xe7f);observed=None
 u.emu_start(0x461de0,stop,count=100000)
 assert observed==tuple(map(int,line.split())),(case,observed,line)
assert len(lines)==len(cases)
random_cases=[(rng.randrange(2**32),rng.randrange(10),rng.randrange(2)) for i in range(1200)]
lines=subprocess.check_output([sys.argv[1],'--rng'],input=''.join(' '.join(map(str,c))+'\n' for c in random_cases),text=True).splitlines()
for (seed,warmup,active),line in zip(random_cases,lines):
 state=scenario+0x218;w(stack,stop);w(stack+4,seed);u.reg_write(UC_X86_REG_ECX,state);u.reg_write(UC_X86_REG_ESP,stack);u.emu_start(0x638790,stop,count=100000)
 for _ in range(warmup):
  w(stack,stop);u.reg_write(UC_X86_REG_ECX,state);u.reg_write(UC_X86_REG_ESP,stack);u.emu_start(0x638840,stop,count=10000)
 u.mem_write(projectile+0x7d,bytes((active,)));u.mem_write(stack+0x18,struct.pack('<iii',12800,12800,0))
 u.reg_write(UC_X86_REG_ESI,projectile);u.reg_write(UC_X86_REG_EDI,0);u.reg_write(UC_X86_REG_ESP,stack);u.reg_write(UC_X86_REG_FPCW,0xe7f)
 u.emu_start(0x462098,stop,count=100000)
 actual=(read(state+4),read(state+8),*struct.unpack('<250I',u.mem_read(state+12,1000)))
 assert actual==tuple(map(int,line.split())),((seed,warmup,active),actual[:2],line[:40])
assert len(lines)==len(random_cases)
print(f'Base game.exe: {len(cases)} Unit impact positions and {len(random_cases)} exact post-impact RNG states match')

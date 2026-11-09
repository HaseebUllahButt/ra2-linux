#!/usr/bin/env python3
"""Compare the guarded complete Unit virtual +364 firing check to game.exe.

Build: g++ -std=c++17 -O2 -Wall -Wextra -Werror -I rebuild/src \
  rebuild/src/weapons/{fire_eligibility,fire_timer,ordinary_fire_test}.cpp rebuild/src/sim/random.cpp -o /tmp/ra2_ordinary_fire
Run: RA2_GAME_DIR=/path/to/game python3 rebuild/src/weapons/verify_ordinary_fire_x86.py /tmp/ra2_ordinary_fire
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
u.mem_map(0x1000000,0x200000);u.mem_map(0x2000000,0x100000);u.mem_map(0x3000000,4096)
base,stack,stop=0x1000000,0x20fff00,0x3000000
actor,target,vt,tt,kind,targetkind,house,cell,weapon,projectile,warhead,slot=(base+n for n in (0x1000,0x3000,0x5000,0x7000,0x9000,0xb000,0xd000,0xf000,0x11000,0x13000,0x15000,0x17000))
def w(p,n):u.mem_write(p,struct.pack('<I',n&0xffffffff))
def read(p):return struct.unpack('<I',u.mem_read(p,4))[0]
def ret(cpu,v,n=0):
 sp=cpu.reg_read(UC_X86_REG_ESP);cpu.reg_write(UC_X86_REG_EAX,v&0xffffffff);cpu.reg_write(UC_X86_REG_EIP,read(sp));cpu.reg_write(UC_X86_REG_ESP,sp+4+n*4)
case=None
def hook(cpu,addr,size,user):
 if addr==stop+0x100:ret(cpu,0)
 elif addr==stop+0x110:ret(cpu,1)
 elif addr==stop+0x120:ret(cpu,slot,1)
 elif addr==stop+0x130:ret(cpu,kind if cpu.reg_read(UC_X86_REG_ECX)==actor else targetkind)
 elif addr==stop+0x140:
  ptr=read(cpu.reg_read(UC_X86_REG_ESP)+4);cpu.mem_write(ptr,b'\0'*12);ret(cpu,ptr,1)
 elif addr==stop+0x150:ret(cpu,0,2)
 elif addr==stop+0x160:ret(cpu,case[1])
 elif addr==stop+0x170:ret(cpu,case[14])
 elif addr==stop+0x180:ret(cpu,cell)
 elif addr==stop+0x190:ret(cpu,case[4])
 elif addr==0x548000:ret(cpu,cell,1)
for addr in (stop+0x100,stop+0x110,stop+0x120,stop+0x130,stop+0x140,stop+0x150,stop+0x160,stop+0x170,stop+0x180,stop+0x190,0x548000):u.hook_add(UC_HOOK_CODE,hook,begin=addr,end=addr)
w(actor,vt);w(target,tt);w(cell,tt);w(actor+0x1b4,house);w(target+0x1b4,house);w(actor+0x5ac,kind)
for off,addr in ((0x1c0,0x100),(0x1bc,0x100),(0x1c4,0x100),(0x324,0x100),(0x2c,0x110),(0x39c,0x120),(0x80,0x130),(0x328,0x170)):
 w(vt+off,stop+addr)
for off,addr in ((0x48,0x140),(0x64,0x150),(0x1bc,0x160),(0x54,0x100),(0x1a4,0x180),(0x50,0x190),(0x80,0x130)):
 w(tt+off,stop+addr)
u.mem_write(target+0x14,b'\1');w(weapon+0x9c,1);w(weapon+0xa0,projectile);w(weapon+0xac,warhead)
w(actor+0x5a8,-1);w(actor+0x310,0);w(kind+0xbd8,-1);w(targetkind+0x9c,0)
rng=random.Random(0x6c9261);cases=[]
for _ in range(3000):
 b=lambda:int(rng.randrange(5)==0)
 cases.append((1-b(),b(),b(),rng.choice((0,1,4)),b(),rng.choice((0,1)),rng.choice((-1,0,100)),
               rng.choice((0,1,5,65)),rng.choice((0,1,10,100,105,165)),rng.choice((0,1,10,-1)),b(),
               rng.choice((0.,.1,.5,1.,2.)),b(),b(),b()))
lines=subprocess.check_output([sys.argv[1]],input=''.join(' '.join(map(str,c))+'\n' for c in cases),text=True).splitlines();counts={}
for case,line in zip(cases,lines):
 (present,immune,flag157,land,cell50,f54c,start,duration,frame,ammo,af2,verse,target325,b47,v328)=case
 w(slot,weapon if present else 0);w(cell+0xec,land);w(kind+0x54c,f54c);w(actor+0x244,start);w(actor+0x24c,duration)
 w(0xa40d2c,frame);w(actor+0x254,ammo)
 for addr,val in ((warhead+0x157,flag157),(kind+0xaf2,af2),(target+0x325,target325),(kind+0xb47,b47)):
  u.mem_write(addr,bytes((val,)))
 u.mem_write(warhead+0xa0,struct.pack('<d',verse))
 w(stack,stop);w(stack+4,target);w(stack+8,0);w(stack+12,0)
 u.reg_write(UC_X86_REG_ECX,actor);u.reg_write(UC_X86_REG_ESP,stack);u.reg_write(UC_X86_REG_FPCW,0xe7f)
 u.emu_start(0x6c9260,stop,count=10000)
 actual=u.reg_read(UC_X86_REG_EAX);expected=int(line)
 assert actual==expected,(case,actual,expected)
 counts[actual]=counts.get(actual,0)+1
assert len(lines)==len(cases) and set(counts)=={0,1,3,5,6,8},counts
print(f'Base game.exe: {len(cases)} ordinary Unit firing checks match; results {counts}')

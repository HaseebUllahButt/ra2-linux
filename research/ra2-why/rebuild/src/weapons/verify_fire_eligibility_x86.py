#!/usr/bin/env python3
"""Compare the firing eligibility prefix with base game.exe.

Build: g++ -std=c++17 -O2 -Wall -Wextra -Werror -I rebuild/src \
  rebuild/src/weapons/fire_eligibility{,_test}.cpp rebuild/src/weapons/fire_timer.cpp \
  rebuild/src/sim/random.cpp -o /tmp/ra2_fire_eligibility
Run: RA2_GAME_DIR=/path/to/game python3 rebuild/src/weapons/verify_fire_eligibility_x86.py /tmp/ra2_fire_eligibility
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
actor,target,vt,tt,kind,attach,house,otherhouse,cell=(base+n for n in (0x1000,0x3000,0x5000,0x7000,0x9000,0xb000,0xd000,0xf000,0x11000))
def w(p,n):u.mem_write(p,struct.pack('<I',n&0xffffffff))
def read(p):return struct.unpack('<I',u.mem_read(p,4))[0]
def ret(cpu,v,n=0):
 sp=cpu.reg_read(UC_X86_REG_ESP);cpu.reg_write(UC_X86_REG_EAX,v&0xffffffff);cpu.reg_write(UC_X86_REG_EIP,read(sp));cpu.reg_write(UC_X86_REG_ESP,sp+4+n*4)
case=None;continued=False
def hook(cpu,addr,size,user):
 global continued
 if addr==stop+0x100:ret(cpu,case[1])
 elif addr==stop+0x110:ret(cpu,case[2])
 elif addr==stop+0x120:ret(cpu,case[4])
 elif addr==stop+0x130:ret(cpu,case[15])
 elif addr==stop+0x140:ret(cpu,case[16])
 elif addr==stop+0x150:
  ptr=read(cpu.reg_read(UC_X86_REG_ESP)+4);cpu.mem_write(ptr,b'\0'*12);ret(cpu,ptr,1)
 elif addr==stop+0x160:ret(cpu,case[10],2)
 elif addr==0x548000:ret(cpu,cell,1)
 elif addr==0x47c2e0:ret(cpu,case[11],1)
 elif addr==0x6c16f0:ret(cpu,case[12],1)
 elif addr==0x4e5540:ret(cpu,case[13],1)
 elif addr==0x6c93bd:continued=True;cpu.emu_stop()
for addr in (stop+0x100,stop+0x110,stop+0x120,stop+0x130,stop+0x140,stop+0x150,stop+0x160,0x548000,0x47c2e0,0x6c16f0,0x4e5540,0x6c93bd):u.hook_add(UC_HOOK_CODE,hook,begin=addr,end=addr)
w(actor,vt);w(target,tt);w(actor+0x1b4,house);w(target+0x1b4,otherhouse);w(actor+0x5ac,kind)
for off,addr in ((0x1c0,0x100),(0x1bc,0x110),(0x1c4,0x120),(0x324,0x130),(0x2c,0x140)):w(vt+off,stop+addr)
w(tt+0x48,stop+0x150);w(tt+0x64,stop+0x160)
rng=random.Random(0x6c9260);cases=[]
for _ in range(5000):
 b=lambda: int(rng.randrange(5)==0)
 cases.append((1-b(),b(),b(),b(),b(),b(),b(),1-b(),b(),b(),rng.choice((0,5)),
               1-b(),rng.choice((0,0,3)),1-b(),b(),b(),rng.choice((1,1,2)),b(),b()))
lines=subprocess.check_output([sys.argv[1]],input=''.join(' '.join(map(str,c))+'\n' for c in cases),text=True).splitlines();counts={}
for case,line in zip(cases,lines):
 (present,v1c0,v1bc,flag325,v1c4,has_attachment,is_attached,is_object,limbo,flag210,
  v64,visible,damage,allied,flag7a,v324,orig_kind,bb0,bb1)=case
 for addr,val in ((actor+0x325,flag325),(actor+0x7a,flag7a),(target+0x14,is_object),
                  (target+0x76,limbo),(target+0x210,flag210),(kind+0xbb0,bb0),(kind+0xbb1,bb1)):
  u.mem_write(addr,bytes((val,)))
 w(actor+0x208,attach if has_attachment else 0);w(attach+0x28,target if is_attached else base+0x13000)
 w(stack,stop);w(stack+4,target if present else 0);w(stack+8,0);w(stack+12,0)
 u.reg_write(UC_X86_REG_ECX,actor);u.reg_write(UC_X86_REG_ESP,stack);continued=False
 u.emu_start(0x6c9260,stop,count=10000)
 actual=0 if continued else u.reg_read(UC_X86_REG_EAX);expected=int(line)
 assert actual==expected,(case,actual,expected)
 counts[actual]=counts.get(actual,0)+1
assert len(lines)==len(cases) and set(counts)=={0,3,5,6},counts
print(f'Base game.exe: {len(cases)} firing eligibility prefixes match; results {counts}')

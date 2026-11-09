#!/usr/bin/env python3
"""Compare the original Unit death trigger prefix and synchronous mutations.

Build: g++ -std=c++17 -O2 -Wall -Wextra -Werror -I rebuild/src \
  rebuild/src/weapons/unit_death{,_test}.cpp -o rebuild/build/unit_death_test
Run: RA2_GAME_DIR=/path/to/game python3 rebuild/src/weapons/verify_unit_death_x86.py rebuild/build/unit_death_test
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
obj,trigger,attacker,vt,kind,kvt=(base+n for n in (0x1000,0x3000,0x5000,0x7000,0x9000,0xb000))
def w(p,n):u.mem_write(p,struct.pack('<I',n&0xffffffff))
def read(p):return struct.unpack('<I',u.mem_read(p,4))[0]
def ret(cpu,v,n=0):
 sp=cpu.reg_read(UC_X86_REG_ESP);cpu.reg_write(UC_X86_REG_EAX,v);cpu.reg_write(UC_X86_REG_EIP,read(sp));cpu.reg_write(UC_X86_REG_ESP,sp+4+n*4)
case=None;events=[]
def hook(cpu,addr,size,user):
 if addr==0x6b41f0:ret(cpu,case[4])
 elif addr==0x6b3df0:
  sp=cpu.reg_read(UC_X86_REG_ESP);events.append(read(sp+4))
  if len(events)==case[5]:
   if case[6]==1:w(obj+0x34,0)
   if case[6]==2:cpu.mem_write(obj+0x7d,b'\0')
  ret(cpu,0,5)
 elif addr==stop+0x100:ret(cpu,kind)
 elif addr==stop+0x110:ret(cpu,10,1)
 elif addr==stop+0x120:ret(cpu,1)
 elif addr==0x6cee82:cpu.emu_stop()
for addr in (0x6b41f0,0x6b3df0,stop+0x100,stop+0x110,stop+0x120,0x6cee82):u.hook_add(UC_HOOK_CODE,hook,begin=addr,end=addr)
w(obj,vt);w(vt+0x80,stop+0x100);w(vt+0x2c,stop+0x120);w(kind,kvt);w(kvt+0x84,stop+0x110)
rng=random.Random(0x707ed0);cases=[]
for _ in range(2000):cases.append((rng.randrange(2),rng.randrange(2),rng.choice((-1,0,1)),rng.randrange(2),rng.randrange(2),rng.randrange(4),rng.randrange(3)))
lines=subprocess.check_output([sys.argv[1]],input=''.join(' '.join(map(str,c))+'\n' for c in cases),text=True).splitlines()
for case,line in zip(cases,lines):
 present,active,field,source,special,mutate_at,mutation=case
 w(obj+0x34,trigger if present else 0);w(obj+0x290,field);u.mem_write(obj+0x7d,bytes((active,)))
 w(stack,stop);w(stack+4,attacker if source else 0);u.reg_write(UC_X86_REG_ECX,obj);u.reg_write(UC_X86_REG_ESP,stack);events=[]
 u.emu_start(0x707ed0,stop,count=10000)
 actual=(int(read(obj+0x34)!=0),int(u.mem_read(obj+0x7d,1)[0]!=0),len(events),*events)
 assert actual==tuple(map(int,line.split())),(case,actual,line)
assert len(lines)==len(cases)
print(f'Base game.exe: {len(cases)} Unit and parent death trigger prefixes match')

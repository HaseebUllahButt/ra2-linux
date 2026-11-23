#!/usr/bin/env python3
"""Compare flat Unit muzzle positions with base game.exe.

Build: g++ -std=c++17 -O2 -Wall -Wextra -Werror -I rebuild/src \
  rebuild/src/weapons/muzzle{,_test}.cpp -o rebuild/build/muzzle_test
Run: RA2_GAME_DIR=/path/to/game python3 rebuild/src/weapons/verify_muzzle_x86.py rebuild/build/muzzle_test
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
actor,vt,kind,slot,loc,locvt,face,out=(base+n for n in (0x1000,0x3000,0x5000,0x7000,0x9000,0xb000,0xd000,0xf000))
def w(p,n):u.mem_write(p,struct.pack('<I',n&0xffffffff))
def read(p):return struct.unpack('<I',u.mem_read(p,4))[0]
def ret(cpu,v,n=0):
 sp=cpu.reg_read(UC_X86_REG_ESP);cpu.reg_write(UC_X86_REG_EAX,v);cpu.reg_write(UC_X86_REG_EIP,read(sp));cpu.reg_write(UC_X86_REG_ESP,sp+4+n*4)
case=None
def hook(cpu,addr,size,user):
 if addr==stop+0x100:ret(cpu,kind)
 elif addr==stop+0x110:ret(cpu,slot,1)
 elif addr==0x4b8be0:
  w(face,case[4] if cpu.reg_read(UC_X86_REG_ECX)==actor+0x2f8 else case[3]);ret(cpu,face,1)
for addr in (stop+0x100,stop+0x110,0x4b8be0):u.hook_add(UC_HOOK_CODE,hook,begin=addr,end=addr)
w(actor,vt);w(vt+0x80,stop+0x100);w(vt+0x39c,stop+0x110);w(vt+0x288,0x70a2e0)
w(vt+0xa8,0x41b270);w(vt+0x48,0x5d4e40);w(actor+0x5ac,kind);u.mem_write(kind+0xaab,b'\1')
w(actor+0x564,loc);w(loc,locvt);w(loc+8,actor);w(locvt+0x24,0x53d030);u.mem_write(actor+0x14,b'\4')
rng=random.Random(0x6c1830);cases=[]
for _ in range(3000):
 facing=rng.randrange(65536);turret=rng.randrange(65536)
 cases.append((rng.randrange(-10000,10001),rng.randrange(-10000,10001),rng.randrange(-1000,1001),facing,turret,
               rng.randrange(-300,301),rng.randrange(-300,301),rng.randrange(-300,301),rng.randrange(-32,33),rng.randrange(4)))
cases.extend((0,0,0,f,t,150,0,100,0,0) for f in range(0,65536,2048) for t in range(0,65536,2048))
lines=subprocess.check_output([sys.argv[1]],input=''.join(' '.join(map(str,c))+'\n' for c in cases),text=True).splitlines()
for case,line in zip(cases,lines):
 x,y,z,hull,turret,flh_x,flh_y,flh_z,offset,burst=case
 u.mem_write(actor+0x88,struct.pack('<iii',x,y,z));w(kind+0x63c,offset);w(actor+0x310,burst)
 w(slot+4,flh_x);w(slot+8,flh_y);w(slot+12,flh_z);w(stack,stop);w(stack+4,out);w(stack+8,0)
 u.reg_write(UC_X86_REG_ECX,actor);u.reg_write(UC_X86_REG_ESP,stack);u.reg_write(UC_X86_REG_FPCW,0xe7f)
 u.emu_start(0x6c1830,stop,count=100000)
 actual=struct.unpack('<iii',u.mem_read(out,12));expected=tuple(map(int,line.split()))
 assert actual==expected,(case,actual,expected)
assert len(lines)==len(cases)
print(f'Base game.exe: {len(cases)} flat Unit muzzle positions match')

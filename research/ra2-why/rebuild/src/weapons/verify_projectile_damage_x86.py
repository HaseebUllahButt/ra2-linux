#!/usr/bin/env python3
"""Compare projectile raw damage before creation with base game.exe.

Build: g++ -std=c++17 -O2 -Wall -Wextra -Werror -I rebuild/src \
  rebuild/src/weapons/projectile_damage{,_test}.cpp -o rebuild/build/projectile_damage_test
Run: RA2_GAME_DIR=/path/to/game python3 rebuild/src/weapons/verify_projectile_damage_x86.py rebuild/build/projectile_damage_test
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
actor,weapon,house,kind,rules,vt=(base+n for n in (0x1000,0x3000,0x5000,0x7000,0x9000,0xb000))
def w(p,n):u.mem_write(p,struct.pack('<I',n&0xffffffff))
def read(p):return struct.unpack('<I',u.mem_read(p,4))[0]
def ret(cpu,v,n=0):
 sp=cpu.reg_read(UC_X86_REG_ESP);cpu.reg_write(UC_X86_REG_EAX,v&0xffffffff);cpu.reg_write(UC_X86_REG_EIP,read(sp));cpu.reg_write(UC_X86_REG_ESP,sp+4+n*4)
case=None

def hook(cpu,addr,size,user):
 if addr==0x7133a0:ret(cpu,case[5])
 elif addr==0x713420:ret(cpu,case[6])
 elif addr==stop+0x100:ret(cpu,kind)
for addr in (0x7133a0,0x713420,stop+0x100):u.hook_add(UC_HOOK_CODE,hook,begin=addr,end=addr)
w(actor,vt);w(vt+0x80,stop+0x100);w(actor+0x1b4,house);w(0x839848,rules)
rng=random.Random(0x6cab7c);cases=[]
for _ in range(5000):
 cases.append((rng.choice((-90,-1,0,1,25,90,400)),rng.randrange(2),rng.randrange(2),
               rng.choice((0.,.25,.5,1.,1.2,1.5,2.)),rng.choice((0.,.25,.5,1.,1.2,1.5,2.)),
               *(rng.randrange(2) for i in range(4)),rng.choice((0.,.5,1.,1.2,1.5,2.))))
lines=subprocess.check_output([sys.argv[1]],input=''.join(' '.join(map(str,c))+'\n' for c in cases),text=True).splitlines()
for case,line in zip(cases,lines):
 damage,sonic,fire,housepow,objpow,veteran,elite,vflag,eflag,vetmult=case
 w(weapon+0xa4,damage);u.mem_write(weapon+0x10c,bytes((sonic,)));u.mem_write(weapon+0x105,bytes((fire,)))
 u.mem_write(house+0xd0,struct.pack('<d',housepow));u.mem_write(actor+0x128,struct.pack('<d',objpow))
 u.mem_write(kind+0x26e,bytes((vflag,)));u.mem_write(kind+0x280,bytes((eflag,)));u.mem_write(rules+0x590,struct.pack('<d',vetmult))
 u.reg_write(UC_X86_REG_ESI,actor);u.reg_write(UC_X86_REG_EBX,weapon);u.reg_write(UC_X86_REG_ESP,stack);u.reg_write(UC_X86_REG_FPCW,0xe7f)
 u.emu_start(0x6cab7c,0x6cac65,count=10000)
 actual=u.reg_read(UC_X86_REG_EDI);actual=actual if actual<0x80000000 else actual-0x100000000
 assert actual==int(line),(case,actual,line)
assert len(lines)==len(cases)
print(f'Base game.exe: {len(cases)} projectile damage values match')

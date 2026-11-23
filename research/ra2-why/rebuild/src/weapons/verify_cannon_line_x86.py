#!/usr/bin/env python3
"""Compare the complete resolved-map Cannon line trace with base game.exe.

Build: g++ -std=c++17 -O2 -Wall -Wextra -Werror -I rebuild/src \
  rebuild/src/weapons/{cannon_line,cannon_line_test,weapons}.cpp \
  rebuild/src/sim/{combat,target,distance}.cpp -o rebuild/build/cannon_line_test
Run: RA2_GAME_DIR=/path/to/game python3 rebuild/src/weapons/verify_cannon_line_x86.py rebuild/build/cannon_line_test
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
u.mem_map(0x1000000,0x1000000);u.mem_map(0x2100000,0x100000);u.mem_map(0x3000000,4096)
base,stack,stop=0x1000000,0x21fff00,0x3000000
weapon,projectile,warhead,rules,owner,table,overlay,vt,origin,dest,cells=(base+n for n in (0x1000,0x3000,0x5000,0x7000,0x9000,0xb000,0xd000,0xf000,0x11000,0x13000,0x20000))
def w(p,n):u.mem_write(p,struct.pack('<I',n&0xffffffff))
def read(p):return struct.unpack('<I',u.mem_read(p,4))[0]
def ret(cpu,v,n=0):
 sp=cpu.reg_read(UC_X86_REG_ESP);cpu.reg_write(UC_X86_REG_EAX,v&0xffffffff);cpu.reg_write(UC_X86_REG_EIP,read(sp));cpu.reg_write(UC_X86_REG_ESP,sp+4+n*4)
case=None;positions={};addresses={};by_address={};next_cell=0

def resolve(x,y):
 global next_cell
 key=(x,y)
 if key not in addresses:
  seed=case[0];h=(x*37+y*73+seed*17)&255;level=h%7-3;bit=(h//7)%2;ov=(h//14)%7
  idx=0 if ov==0 else 2 if ov==1 else 26 if ov==2 else -1
  p=cells+next_cell*0x1000;next_cell+=1;addresses[key]=p;by_address[p]=y*512+x+1
  w(p,vt);w(p+0x44,idx);w(p+0x140,0x80 if bit else 0);w(p+0x50,0)
  u.mem_write(p+0x11b,bytes((level&255,)));positions[p]=(x*256+128,y*256+128,level*104)
 return addresses[key]
def hook(cpu,addr,size,user):
 if addr==0x548070:
  ptr=read(cpu.reg_read(UC_X86_REG_ESP)+4);x,y=struct.unpack('<hh',cpu.mem_read(ptr,4));ret(cpu,resolve(x,y),1)
 elif addr==stop+0x100:
  ptr=read(cpu.reg_read(UC_X86_REG_ESP)+4);cpu.mem_write(ptr,struct.pack('<iii',*positions[cpu.reg_read(UC_X86_REG_ECX)]));ret(cpu,ptr,1)
for addr in (0x548070,stop+0x100):u.hook_add(UC_HOOK_CODE,hook,begin=addr,end=addr)
w(vt+0x48,stop+0x100);w(weapon+0xa0,projectile);w(weapon+0xac,warhead)
w(0x839848,rules);w(0xa35dec,table);w(0xa3229c,owner)
for idx in (0,2,26):w(table+idx*4,overlay)
u.mem_write(overlay+0x278,b'\1');w(owner,0)
rng=random.Random(0x4bbaa0);cases=[]
for _ in range(2400):
 src=(rng.randrange(50*256,60*256),rng.randrange(50*256,60*256),rng.randrange(-200,201))
 dst=(rng.randrange(50*256,60*256),rng.randrange(50*256,60*256),rng.randrange(-200,201))
 cases.append((rng.randrange(20),*src,*dst,*(rng.randrange(2) for i in range(4))))
lines=subprocess.check_output([sys.argv[1]],input=''.join(' '.join(map(str,c))+'\n' for c in cases),text=True).splitlines();counts={}
for case,line in zip(cases,lines):
 seed,x,y,z,tx,ty,tz,cliffs,walls,rule,ignore=case
 addresses={};by_address={};positions={};next_cell=0
 u.mem_write(origin,struct.pack('<iii',x,y,z));u.mem_write(dest,struct.pack('<iii',tx,ty,tz))
 u.mem_write(projectile+0x266,bytes((cliffs,0,walls)));u.mem_write(rules+0x1498,bytes((rule,)));u.mem_write(warhead+0x144,bytes((ignore,)))
 w(stack,stop);w(stack+4,weapon);w(stack+8,0);u.reg_write(UC_X86_REG_ECX,origin);u.reg_write(UC_X86_REG_EDX,dest)
 u.reg_write(UC_X86_REG_ESP,stack);u.reg_write(UC_X86_REG_FPCW,0xe7f);u.emu_start(0x4bbaa0,stop,count=200000)
 hit=u.reg_read(UC_X86_REG_EAX);actual=(1,by_address[hit]) if hit else (0,0);expected=tuple(map(int,line.split()))
 assert actual==expected,(case,actual,expected)
 counts[actual[0]]=counts.get(actual[0],0)+1
assert len(lines)==len(cases) and set(counts)=={0,1},counts
print(f'Base game.exe: {len(cases)} complete Cannon line traces match; results {counts}')

#!/usr/bin/env python3
"""Check the guarded world area collection against base game.exe.

Build: g++ -std=c++17 -O2 -Wall -Wextra -Werror -I rebuild/src \
  rebuild/src/weapons/world_area_test.cpp rebuild/src/sim/damage_runtime.cpp \
  rebuild/src/weapons/weapons.cpp rebuild/src/sim/{combat,target,distance}.cpp -o /tmp/ra2_world_area
Run: RA2_GAME_DIR=/path/to/game python3 rebuild/src/weapons/verify_world_area_x86.py /tmp/ra2_world_area
"""
import os
from pathlib import Path
import random
import re
import struct
import subprocess
import sys
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE
from unicorn.x86_const import *
u=Uc(UC_ARCH_X86,UC_MODE_32)
u.mem_map(0x400000,0x800000);u.mem_write(0x400000,(Path(os.environ['RA2_GAME_DIR'])/'game.exe').read_bytes()[:0x431000])
u.mem_map(0x1000000,0x200000);u.mem_map(0x2000000,0x100000);u.mem_map(0x3000000,0x1000)
base,stack,stop=0x1000000,0x20fff00,0x3000000
scenario,warhead,position,vtable,cells,objs,heap=(base+n for n in (0x1000,0x3000,0x5000,0x7000,0x10000,0x50000,0x100000))
def w(p,n):u.mem_write(p,struct.pack('<I',n&0xffffffff))
def read(p):return struct.unpack('<I',u.mem_read(p,4))[0]
def signed(n):return n if n<0x80000000 else n-0x100000000
def ret(cpu,val,n=0):
 sp=cpu.reg_read(UC_X86_REG_ESP);cpu.reg_write(UC_X86_REG_EAX,val&0xffffffff);cpu.reg_write(UC_X86_REG_EIP,read(sp));cpu.reg_write(UC_X86_REG_ESP,sp+4+4*n)
w(stack,stop);u.reg_write(UC_X86_REG_ESP,stack);u.emu_start(0x5441e0,stop,count=100000)
assert list(struct.iter_unpack('<hh',u.mem_read(0xa6f248,36)))==[(0,0),(1,-1),(0,-1),(-1,-1),(-1,0),(1,0),(-1,1),(0,1),(1,1)]
header=Path(__file__).resolve().parents[1]/'sim'/'blast_offsets.h'
offsets=[tuple(map(int,m)) for m in re.findall(r'\{\{(-?\d+),(-?\d+)\}\}',header.read_text())]
assert offsets==list(struct.iter_unpack('<hh',u.mem_read(0xa6f248,309*4)))
w(0xa3d290,scenario);w(0x850cd8,104);w(vtable+0x2c,stop+0x100);w(vtable+0xa0,stop+0x110);w(vtable+0x32c,stop+0x120)
coords={};kinds={};shields={};mapping={};allocated=heap;result=None;special=None

def hook(cpu,addr,size,user):
 global allocated,result,special
 sp=cpu.reg_read(UC_X86_REG_ESP); ecx=cpu.reg_read(UC_X86_REG_ECX)
 if addr==0x548070:
  ptr=read(sp+4);x,y=struct.unpack('<hh',cpu.mem_read(ptr,4));ret(cpu,mapping[(x,y)],1)
 elif addr==0x55a280:ret(cpu,0,1)
 elif addr==stop+0x100:ret(cpu,kinds[ecx])
 elif addr==stop+0x110:
  ptr=read(sp+4);cpu.mem_write(ptr,struct.pack('<iii',*coords[ecx]));ret(cpu,ptr,1)
 elif addr==stop+0x120:ret(cpu,shields[ecx])
 elif addr==0x782017:
  n=read(sp+4);p=allocated;allocated+=(n+15)&~15;ret(cpu,p)
 elif addr==0x47fed0:
  cap=read(sp+4);old=read(ecx+4);oldcap=read(ecx+8);p=allocated;allocated+=(cap*4+15)&~15
  if oldcap:cpu.mem_write(p,bytes(cpu.mem_read(old,oldcap*4)))
  w(ecx+4,p);w(ecx+8,cap);cpu.mem_write(ecx+0xd,b'\1');ret(cpu,1,2)
 elif addr==0x47e9c8:
  arr=read(sp+0x40);count=read(sp+0x4c);result=[];special=cpu.mem_read(sp+0x13,1)[0]
  for i in range(count):
   node=read(arr+4*i);result.extend((read(node),signed(read(node+4))))
  cpu.emu_stop()
for addr in (0x548070,0x55a280,stop+0x100,stop+0x110,stop+0x120,0x782017,0x47fed0,0x47e9c8):u.hook_add(UC_HOOK_CODE,hook,begin=addr,end=addr)
rng=random.Random(0x47e2c0);cases=[]
def nearby_float(value, delta):
 bits=struct.unpack('<I',struct.pack('<f',value))[0]
 return struct.unpack('<f',struct.pack('<I',bits+delta))[0]
for k in range(600):
 impact=(12800+rng.randrange(256),16640+rng.randrange(256),0)
 spread=rng.choice((0.,.3,.5,1.,1.1,1.5,2.,3.,4.,5.,6.,7.,8.,9.,10.,
                    *(nearby_float(n+.01,d) for n in range(10) for d in (-1,0,1))))
 entries=[]
 for j in range(rng.randrange(0,12)):
  limit=1 if spread<=.5 else 10
  dx=rng.randrange(-limit,limit+1);dy=rng.randrange(-limit,limit+1);ident=objs+j*0x1000
  pos=(impact[0]+dx*256+rng.randrange(-120,121),impact[1]+dy*256+rng.randrange(-120,121),rng.randrange(-100,101))
  kind=rng.choice((1,15))
  entries.append((ident,dx,dy,*pos,rng.randrange(2),kind,rng.randrange(2)))
 source=rng.choice([0,base+0x60000]+[e[0] for e in entries]);cases.append((impact,spread,source,entries))
# A protected infantry or vehicle at the center changes small-blast dispatch.
# Distance 84 is inside the 85-unit boundary; 85 is outside it.
for kind in (1,15):
 for spread in (.3,.5,1.):
  for dx in (0,84,85):
   impact=(12928,16768,0)
   entries=[(objs,0,0,impact[0]+dx,impact[1],0,1,kind,1),
            (objs+0x1000,0,0,impact[0],impact[1],0,1,15,0)]
   cases.append((impact,spread,0,entries))
def line(case):
 impact,spread,source,entries=case
 return ' '.join(map(str,(*impact,spread,source,len(entries),*(v for e in entries for v in e))))+'\n'
lines=subprocess.check_output([sys.argv[1]],input=''.join(map(line,cases)),text=True,env=os.environ).splitlines()
for case,line in zip(cases,lines):
 impact,spread,source,entries=case;cx,cy=impact[0]//256,impact[1]//256
 mapping={};coords={};kinds={};shields={};allocated=heap;result=None;special=None
 for dy in range(-10,11):
  for dx in range(-10,11):
   cell=cells+((dy+10)*21+(dx+10))*0x200;mapping[(cx+dx,cy+dy)]=cell
   w(cell+0x44,-1);w(cell+0x140,0);w(cell+0xe4,0)
 for ident,dx,dy,x,y,z,active,kind,shield in entries:
  cell=mapping[(cx+dx,cy+dy)];w(ident,vtable);w(ident+0x30,read(cell+0xe4));w(cell+0xe4,ident)
  u.mem_write(ident+0x7d,bytes([active]));u.mem_write(ident+0x14,b'\1');coords[ident]=(x,y,z);kinds[ident]=kind;shields[ident]=shield
 u.mem_write(warhead+0x124,struct.pack('<f',spread));u.mem_write(position,struct.pack('<iii',*impact))
 w(stack,stop);w(stack+4,source);w(stack+8,warhead);w(stack+12,1)
 u.reg_write(UC_X86_REG_ECX,position);u.reg_write(UC_X86_REG_EDX,90);u.reg_write(UC_X86_REG_ESP,stack);u.reg_write(UC_X86_REG_FPCW,0xe7f)
 u.emu_start(0x47e2c0,stop,count=500000)
 native=list(map(int,line.split()));assert result is not None and [special,len(result)//2,*result]==native,(case,special,result,native)
assert len(lines)==len(cases)
print(f'Base game.exe: {len(cases)} ordered ground area collections match')

#!/usr/bin/env python3
"""Compare the base Unit on-map check with resolved original cell data.

Run with game.exe and the existing geometry_cases binary. The original local
rectangle clipping and 0055A660 instructions run for every case.
"""
import pathlib
import struct
import subprocess
import sys
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_ESP
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[2]/'sim'))
from original_image import load_original
if len(sys.argv)!=3: raise SystemExit('usage: original_base_local_check.py game.exe geometry_cases')
exe,native=sys.argv[1:]
cpu=load_original(exe);cpu.mem_map(0x1000000,0x300000)
mapobj,local,point,cell,grid,stack,end=0x1000000,0x1002000,0x1003000,0x1004000,0x1100000,0x12fd000,0x12ff000
def put(addr,*values): cpu.mem_write(addr,struct.pack('<'+'I'*len(values),*(v&0xffffffff for v in values)))
def words(addr,n): return struct.unpack('<'+'i'*n,cpu.mem_read(addr,4*n))
put(0x832618,grid)
previous=None
lines=subprocess.check_output([native],text=True).splitlines()
for line in lines:
    w,h,lx,ly,lw,lh,nx,ny,nw,nh,x,y,height,ramp,unused,expected=map(int,line.split())
    put(mapobj+0xe8,0,0,w,h);put(local,lx,ly,lw,lh);put(stack,end,local)
    cpu.reg_write(UC_X86_REG_ECX,mapobj);cpu.reg_write(UC_X86_REG_ESP,stack)
    cpu.emu_start(0x549ae0,0x549b83,count=1000)
    assert words(mapobj+0xf8,4)==(nx,ny,nw,nh),(line,words(mapobj+0xf8,4))
    index=y*512+x
    if previous is not None: put(grid+4*previous,0)
    previous=index if 0<=index<0x40000 else None
    if previous is not None: put(grid+4*previous,cell)
    cpu.mem_write(cell+0x11b,struct.pack('<bB',height,ramp))
    cpu.mem_write(0xa6f9f8+0x11b,struct.pack('<bB',height,ramp))
    cpu.mem_write(point,struct.pack('<hh',x,y));put(stack,end,point,1)
    cpu.reg_write(UC_X86_REG_ECX,mapobj);cpu.reg_write(UC_X86_REG_ESP,stack)
    cpu.emu_start(0x55a660,end,count=1000)
    assert cpu.reg_read(UC_X86_REG_EAX)&0xff==expected,line
print(f'{len(lines)} base Unit on-map cases match original x86')

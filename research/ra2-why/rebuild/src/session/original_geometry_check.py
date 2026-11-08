#!/usr/bin/env python3
"""Compare original starting-cell geometry with explicit live Cell values.

Requires Unicorn. The original map clipping and validity instructions run.
No terrain, occupancy, or unit placement decisions are substituted here.
"""
import pathlib
import struct
import subprocess
import sys
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_ESP
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parent.parent/'sim'))
from original_image import load_original
if len(sys.argv)!=3: raise SystemExit('usage: original_geometry_check.py game.exe geometry_cases')
exe,native=sys.argv[1:]
base=pathlib.Path(exe).name.lower()=='game.exe'
cpu=load_original(exe);cpu.mem_map(0x1000000,0x300000)
mapobj,local,point,cell,grid,stack,end=0x1000000,0x1002000,0x1003000,0x1004000,0x1100000,0x12fd000,0x12ff000
def put(addr,*values):cpu.mem_write(addr,struct.pack('<'+'I'*len(values),*(v&0xffffffff for v in values)))
def words(addr,n):return struct.unpack('<'+'i'*n,cpu.mem_read(addr,4*n))
if not base:put(0x87f924,grid)
previous=None
lines=subprocess.check_output([native],text=True).splitlines()
for line in lines:
    w,h,lx,ly,lw,lh,nx,ny,nw,nh,x,y,height,ramp,expected_base,expected_yuri=map(int,line.split())
    if base:
        put(mapobj+0xf0,w,h)
    else:
        # 00567230 clips LocalSize and then applies the original edge limits.
        put(mapobj+0xec,0,0,w,h)
        put(local,lx,ly,lw,lh)
        put(stack,end,local)
        cpu.reg_write(UC_X86_REG_ECX,mapobj);cpu.reg_write(UC_X86_REG_ESP,stack)
        cpu.emu_start(0x567230,0x5672d3,count=1000)
        assert words(mapobj+0xfc,4)==(nx,ny,nw,nh),(line,'local clip',words(mapobj+0xfc,4))
        index=y*512+x
        if previous is not None:put(grid+4*previous,0)
        previous=index if 0<=index<0x40000 else None
        if previous is not None:put(grid+4*previous,cell)
        # A null/out-of-range lookup uses the original default Cell object.
        cpu.mem_write(cell+0x11b,struct.pack('<bB',height,ramp))
        cpu.mem_write(0xabdc50+0x11b,struct.pack('<bB',height,ramp))
    cpu.mem_write(point,struct.pack('<hh',x,y))
    put(stack,end,point,1)
    cpu.reg_write(UC_X86_REG_ECX,mapobj);cpu.reg_write(UC_X86_REG_ESP,stack)
    cpu.emu_start(0x54aab0 if base else 0x578460,end,count=1000)
    actual=cpu.reg_read(UC_X86_REG_EAX)&0xff
    assert actual==(expected_base if base else expected_yuri),(line,actual)
print(f'{len(lines)} map geometry cases match original x86: {pathlib.Path(exe).name}')

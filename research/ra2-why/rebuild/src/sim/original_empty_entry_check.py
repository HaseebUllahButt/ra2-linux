#!/usr/bin/env python3
"""Check the original ordinary drive Unit entry on resolved empty flat cells.
The original Unit, height gate, tube lookup, and drive locomotor entry all execute.
Only the map's usable-area answer is adapted. Both editions use real table layouts.
"""
import struct
import sys
from itertools import product
import original_route_check as r
from unicorn import UcError
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_ESP,UC_X86_REG_EIP
m=r.m; cpu=m.cpu; base=m.base
r.setup((0,),15)
m.word(m.VT+(0x198 if base else 0x1b0),0x4c8730 if base else 0x4d9c60)
m.word(m.FOOT+(0x564 if base else 0x674),m.LOCO+4)
m.virtual(0x2f4 if base else 0x320,0,m.ret(0))
m.bind(0x55a740 if base else 0x578540,8,m.ret(1))
source=m.cell_for(54,69); destination=m.cell_for(54,68)
for x in range(52,57):
 for y in range(66,71):
  cell=m.cell_for(x,y); cpu.mem_write(cell+0x116,struct.pack('<h',-1))
  m.word(cell+0x54,-1); m.word(cell+0x58,-1)
h=2166136261; cases=0
for restricted,land,direction,multiplier,profile,usable,on_map in product(
    (-1,0,1,2),(0,1,2,3),(-1,0,1,2,3,4,5,6,7),(0.0,1.0),
    ((0,0,0),(3,0,3),(3,1,0),(-4,0,19)),(0,1),(0,1)):
    level,source_ramp,destination_ramp=profile
    cpu.mem_write(m.FOOT+(0x32d if base else 0x3d5),bytes([on_map]))
    m.callbacks[0x55a740 if base else 0x578540]=m.ret(usable)
    for cell,ramp in ((source,source_ramp),(destination,destination_ramp)):
        cpu.mem_write(cell+0x11b,struct.pack('<bB',level,ramp))
    m.word(m.TYPE+(0xb94 if base else 0xdfc),restricted)
    m.word(destination+0xec,land)
    table=0x850ea0 if base else 0x89ea40
    cpu.mem_write(table+land*9*4,struct.pack('<f',multiplier))
    cpu.reg_write(UC_X86_REG_ECX,m.FOOT); cpu.reg_write(UC_X86_REG_ESP,0x103f000)
    cpu.mem_write(0x103f000,struct.pack('<6I',0x103e000,destination,direction&0xffffffff,level&0xffffffff,source,1))
    try: cpu.emu_start(0x703040 if base else 0x73f0a0,0x103e000,count=10000)
    except UcError:
        print('unresolved empty entry dependency',hex(cpu.reg_read(UC_X86_REG_EIP)),[hex(a) for a in m.recent]); raise
    assert cpu.reg_read(UC_X86_REG_EIP)==0x103e000,'empty-entry instruction bound exceeded'
    result=cpu.reg_read(UC_X86_REG_EAX)
    expected=7 if (restricted!=-1 and restricted!=land) or multiplier==0 or (on_map and not usable) else 0
    assert result==expected,(restricted,land,direction,multiplier,profile,usable,result,expected)
    h=((h^result)*16777619)&0xffffffff; cases+=1
print('original empty Unit cell entry cases=%d hash=%08X'%(cases,h))

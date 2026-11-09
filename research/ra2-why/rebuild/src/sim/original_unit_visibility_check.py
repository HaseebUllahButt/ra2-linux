#!/usr/bin/env python3
"""Check base 00568140 projection and Cell +12C reads with original x86.
The fixture supplies allocated Cell records and the projection divisor.
"""
import struct
import sys
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_EIP,UC_X86_REG_ESP
if len(sys.argv)!=2: raise SystemExit('usage: original_unit_visibility_check.py /path/to/game.exe')
exe=sys.argv[1];sys.argv=['original_route_check.py',exe,'--base']
import original_route_check as r
m=r.m;cpu=m.cpu;r.setup((0,),15);m.word(0xa6fc30,104)
point=0x100d000
count=0
for height in range(4):
    for first in (0,8):
        for second in (0,8):
            offset=height//2+(height&1);x=20-offset;y=20-offset
            a=m.cell_for(x,y);b=m.cell_for(x+1,y+1)
            cpu.mem_write(a+0x12c,bytes([first]));cpu.mem_write(b+0x12c,bytes([second]))
            m.coord(point,(20*256+128,20*256+128,height*104))
            cpu.reg_write(UC_X86_REG_ESP,0x103f000);cpu.mem_write(0x103f000,struct.pack('<II',0x103e000,point))
            cpu.emu_start(0x568140,0x103e000,count=10000)
            assert cpu.reg_read(UC_X86_REG_EIP)==0x103e000
            actual=cpu.reg_read(UC_X86_REG_EAX)&255
            expected=int(first==0 and (not(height&1) or second==0))
            assert actual==expected,(height,first,second,actual,expected)
            count+=1
print(count,'base original Cell visibility projections match')

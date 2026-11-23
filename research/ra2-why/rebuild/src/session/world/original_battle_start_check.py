#!/usr/bin/env python3
"""Compare Battle House start coordinate writes with original base x86.

The selected waypoint is supplied. Instructions 005B7C74..005B7CA2 run
without stubs and write all three House fields. Selection and later map phases
are separate checks.
"""
import struct
import subprocess
import sys
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_EBX,UC_X86_REG_ESP
if len(sys.argv)!=3:
    raise SystemExit('usage: original_battle_start_check.py game.exe setup_case')
exe,native=sys.argv[1:]
sys.argv=[sys.argv[0],exe,'--base']
import original_move_trace as m
point=0x100d000
house=0x1400000
m.cpu.mem_map(house,0x10000)
cases=[(0,0),(135,80),(1,511),(32767,-32768),(-1,-1),(-32768,32767)]
expected=[]
for x,y in cases:
    m.cpu.mem_write(point,struct.pack('<hh',x,y))
    m.cpu.mem_write(house+0x53bc,b'\xa5'*12)
    m.cpu.reg_write(UC_X86_REG_EAX,point)
    m.cpu.reg_write(UC_X86_REG_EBX,house)
    m.cpu.reg_write(UC_X86_REG_ESP,0x103f000)
    m.cpu.emu_start(0x5b7c74,0x5b7ca3,count=100)
    expected.append(tuple(struct.unpack('<iii',m.cpu.mem_read(house+0x53bc,12))))
raw=''.join(f'{x} {y}\n' for x,y in cases)
actual=[tuple(map(int,line.split())) for line in
        subprocess.check_output([native,'unused','--starts'],input=raw,text=True).splitlines()]
assert actual==expected,(actual,expected)
assert expected[1]==(34688,20608,0)
print('base x86 Battle start writes match native cell centers and Z zero')

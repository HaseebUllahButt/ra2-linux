#!/usr/bin/env python3
"""Compare bridge bit 100 cells with both original base bridge setters.

Cell lookup, redraw services and the original direction table are fixture
inputs. The two original setters execute their own flag writes.
"""
import struct
import subprocess
import sys
from unicorn.x86_const import UC_X86_REG_ECX,UC_X86_REG_ESP
if len(sys.argv)!=3:
    raise SystemExit('usage: original_bridge_check.py game.exe bridge_footprint_case')
exe,native=sys.argv[1:]
sys.argv=[sys.argv[0],exe,'--base']
import original_move_trace as m
m.bind(0x631570,4,m.ret(0))
m.bind(0x473000,0,m.ret(0))
directions=((0,-1),(1,-1),(1,0),(1,1),(0,1),(-1,1),(-1,0),(-1,-1))
for i,(x,y) in enumerate(directions):
    m.cpu.mem_write(0x851a70+i*4,struct.pack('<hh',x,y))
for y in range(17,24):
    for x in range(17,24): m.cell_for(x,y)
expected=[]
for address in (0x4732d0,0x473700):
    for direction in range(8):
        for initial in range(2):
            for enabled in range(2):
                for cell in m.cells.values(): m.word(cell+0x140,0x100 if initial else 0)
                root=m.cell_for(20,20)
                m.cpu.reg_write(UC_X86_REG_ECX,root)
                m.cpu.reg_write(UC_X86_REG_ESP,0x103f000)
                m.cpu.mem_write(0x103f000,struct.pack('<III',0x103e000,direction,enabled))
                m.cpu.emu_start(address,0x103e000,count=100000)
                # Fresh cells retain the root marker only at the root.
                for (x,y),cell in m.cells.items():
                    assert bool(m.read(cell+0x140)&0x80)==(bool(enabled) and (x,y)==(20,20))
                expected.append(''.join(str(int(bool(m.read(m.cell_for(x,y)+0x140)&0x100)))
                                        for y in range(17,24) for x in range(17,24)))
actual=subprocess.check_output([native],text=True).splitlines()
assert actual==expected, next(((i,a,b) for i,(a,b) in enumerate(zip(actual,expected)) if a!=b),None)
assert len(actual)==len(expected)==64
print('3136 bridge-flag observations match both original base setters')

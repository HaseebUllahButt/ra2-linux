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
# Execute the original Overlay entry to the return from its bridge setter.
# Placement eligibility, its cell getter, and map services are supplied.
m.bind(0x5d4160,4,m.ret(1))
m.word(m.FOOT,m.VT);m.word(m.FOOT+0x98,m.TYPE)
point=0x100d000
m.word(point,20|(20<<16));m.virtual(0x1a0,4,m.ret(point))
for kind in (24,25,237,238):
    for mode in (1,3):
        for ramp in (0,4):
            for cell in m.cells.values(): m.word(cell+0x140,0)
            root=m.cell_for(20,20)
            m.cpu.mem_write(root+0x11c,bytes((ramp,)))
            m.word(m.TYPE+0x264,kind)
            m.cpu.reg_write(UC_X86_REG_ECX,m.FOOT)
            m.cpu.reg_write(UC_X86_REG_ESP,0x103f000)
            m.cpu.mem_write(0x103f000,struct.pack('<II',0x103e000,mode))
            m.cpu.emu_start(0x5da970,0x5daa31,count=100000)
            expected.append(''.join(str(int(bool(m.read(m.cell_for(x,y)+0x140)&0x100)))
                                    for y in range(17,24) for x in range(17,24)))
# These branches exit before root creation.
for kind in (24,25,237,238):
    for allowed,mode,ramp in ((0,1,0),(1,2,0),(1,1,5)):
        for cell in m.cells.values(): m.word(cell+0x140,0)
        root=m.cell_for(20,20)
        m.cpu.mem_write(root+0x11c,bytes((ramp,)))
        m.word(m.TYPE+0x264,kind)
        m.bind(0x5d4160,4,m.ret(allowed))
        m.cpu.reg_write(UC_X86_REG_ECX,m.FOOT)
        m.cpu.reg_write(UC_X86_REG_ESP,0x103f000)
        m.cpu.mem_write(0x103f000,struct.pack('<II',0x103e000,mode))
        m.cpu.emu_start(0x5da970,0x5dab84,count=100000)
        assert all(not (m.read(cell+0x140)&0x100) for cell in m.cells.values())
actual=subprocess.check_output([native],text=True).splitlines()
assert actual==expected, next(((i,a,b) for i,(a,b) in enumerate(zip(actual,expected)) if a!=b),None)
assert len(actual)==len(expected)==80
print('3136 setter and 784 Overlay root cell observations match original base x86; 12 exits match')

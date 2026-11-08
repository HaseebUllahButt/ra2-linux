#!/usr/bin/env python3
"""Compare resolved subcell writes against original base 00508410/005084A0.

The map lookup, ground height, subcell and object owner are fixture inputs.
The original layer test and Cell writes run in x86.
"""
import struct
import subprocess
import sys
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_ESP

if len(sys.argv)!=3:
    raise SystemExit('usage: original_cell_occupancy_check.py game.exe cell_occupancy_case')
exe,native=sys.argv[1:]
sys.argv=[sys.argv[0],exe,'--base']
import original_move_trace as m
cpu=m.cpu
point=0x100d000
m.word(m.FOOT,m.VT)
m.virtual(0x38,0,m.ret(7))
m.bind(0x55a280,4,m.ret(0))
m.word(0xa41120,0)
expected=[]
for bridge in range(2):
  for z in (-1,0,1):
    for slot in range(5):
        m.bind(0x4763d0,0,m.ret(slot))
        for bits in (0,1,4,7,28,255):
            for remove in range(2):
                cell=m.cell_for(0,0)
                cpu.mem_write(cell+0x124,bytes((bits,)))
                cpu.mem_write(cell+0x128,bytes((bits^0x55,)))
                m.word(cell+0x54,3); m.word(cell+0x58,9)
                m.word(cell+0x140,0x100 if bridge else 0)
                m.coord(point,(128,128,z))
                cpu.reg_write(UC_X86_REG_ECX,m.FOOT)
                cpu.reg_write(UC_X86_REG_ESP,0x103f000)
                cpu.mem_write(0x103f000,struct.pack('<II',0x103e000,point))
                cpu.emu_start(0x5084a0 if remove else 0x508410,0x103e000,count=10000)
                ground=cpu.mem_read(cell+0x124,1)[0]
                upper=cpu.mem_read(cell+0x128,1)[0]
                expected.append(f'{ground} {upper} {m.signed(cell+0x54)} {m.signed(cell+0x58)}')
scenario=0x100e000
m.word(0xa3d290,scenario)
m.word(m.FOOT+0xb0,m.TYPE)
for session in range(2):
    m.word(scenario+0x838,session)
    for mask in range(8):
        m.word(m.TYPE+0x278,mask if session==0 else mask^7)
        m.word(m.TYPE+0x27c,mask if session==1 else mask^7)
        for bits in (0,1,4,7,28,255):
            for remove in range(2):
                cell=m.cell_for(0,0)
                cpu.mem_write(cell+0x124,bytes((bits,)))
                m.coord(point,(128,128,0))
                cpu.reg_write(UC_X86_REG_ECX,m.FOOT)
                cpu.reg_write(UC_X86_REG_ESP,0x103f000)
                cpu.mem_write(0x103f000,struct.pack('<II',0x103e000,point))
                cpu.emu_start(0x6e3850 if remove else 0x6e38f0,0x103e000,count=10000)
                expected.append(str(cpu.mem_read(cell+0x124,1)[0]))
actual=subprocess.check_output([native],text=True).splitlines()
assert actual==expected, next(((i,a,b) for i,(a,b) in enumerate(zip(actual,expected)) if a!=b),None)
assert len(actual)==len(expected)==552
print('360 subcell and 192 Terrain occupancy writes match original base x86')

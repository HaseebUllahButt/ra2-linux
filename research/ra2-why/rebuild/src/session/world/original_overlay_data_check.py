#!/usr/bin/env python3
"""Compare MP02T2 OverlayDataPack writes with the base x86 scan.

The decoded asset bytes and allocated Cell lookup are supplied. The original
512-by-512 scan, map membership test, and Cell +11E writes execute. Earlier
Overlay object construction and all later map phases are outside this check.
"""
import struct
import subprocess
import sys
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ESP
if len(sys.argv)!=4:
    raise SystemExit('usage: original_overlay_data_check.py game.exe setup_case game_dir')
exe,native,game=sys.argv[1:]
data=subprocess.check_output([native,game,'--overlay-data'])
rows=subprocess.check_output([native,game,'--overlay-data-cells'],text=True).splitlines()
expected={int(key):int(value) for key,value in (row.split() for row in rows)}
assert len(expected)==19900
if not data:
    assert all(value==-1 for value in expected.values())
    print('MP02T2 has no OverlayDataPack; native phase preserves unknown values')
    raise SystemExit(0)
assert len(data)==512*512
sys.argv=[sys.argv[0],exe,'--base']
import original_move_trace as m
base=0x1400000
m.cpu.mem_map(base,0xa00000)
positions={key:base+i*0x200 for i,key in enumerate(sorted(expected))}
for ptr in positions.values(): m.cpu.mem_write(ptr+0x11e,b'\xa5')
m.word(0x8324e0+0xf0,100);m.word(0x8324e0+0xf4,100)
cursor=[0]
def read_byte():
    assert cursor[0]<len(data)
    m.cpu.mem_write(m.argument(0),data[cursor[0]:cursor[0]+1])
    cursor[0]+=1
def lookup():
    x,y=struct.unpack('<hh',m.cpu.mem_read(m.argument(0),4))
    assert y*512+x in positions
    m.cpu.reg_write(UC_X86_REG_EAX,positions[y*512+x])
m.bind(0x535380,8,read_byte)
m.bind(0x548070,4,lookup)
m.cpu.reg_write(UC_X86_REG_ESP,0x103f000)
m.cpu.emu_start(0x5db983,0x5db9e4,count=20000000)
assert cursor[0]==len(data)
for key,value in expected.items():
    actual=m.cpu.mem_read(positions[key]+0x11e,1)[0]
    assert actual==value,(key,actual,value)
print('MP02T2 native OverlayDataPack writes match 19900 original x86 Cells')

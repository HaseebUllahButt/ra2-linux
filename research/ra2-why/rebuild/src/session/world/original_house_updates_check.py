#!/usr/bin/env python3
"""Compare four Battle House counter clears with the original base code.

The original 005B7F01 dispatch and all four 00491D70 bodies run. The counter
sizes and original counter contents are supplied, including empty arrays.
The constructor vtable identity is checked separately with the real House
constructor. Other House updates and the unit driver are outside this slice.
"""
import pathlib
import struct
import subprocess
import sys
from unicorn.x86_const import UC_X86_REG_ESI,UC_X86_REG_ESP
if len(sys.argv)!=3:
    raise SystemExit('usage: original_house_updates_check.py game.exe house_updates_case')
exe,native=sys.argv[1:]
probe=pathlib.Path(__file__).with_name('original_house_probe.py')
lines=subprocess.check_output([sys.executable,str(probe),exe,'--base','42','55','0','--updates'],text=True).splitlines()
updates=[line.split() for line in lines if line.startswith('UPDATE_VTABLE ')]
assert [line[1:] for line in updates]==[[hex(offset),'0x79e5ac','0x491d70','0']
                                       for offset in (0x54a0,0x54d0,0x54c0,0x54b0)]
sys.argv=[sys.argv[0],exe,'--base']
import original_move_trace as m
house=0x1400000
m.cpu.mem_map(house,0x20000)
scenario=house+0x18000
m.word(0xa3d290,scenario)
rng=bytes((i*37+11)&255 for i in range(1012))
m.cpu.mem_write(scenario+0x218,rng)
assert m.read(0x79e5ac+12)==0x491d70
expected=[]
for seed in range(64):
    for i,offset in enumerate((0x54a0,0x54d0,0x54c0,0x54b0)):
        count=(seed*(i+1)+i)%7
        array=house+0x10000+i*0x100
        m.word(house+offset,0x79e5ac)
        m.word(house+offset+4,array)
        m.word(house+offset+8,count)
        for j in range(count): m.word(array+j*4,seed*17+i*5+j+1)
        m.word(array+count*4,0xaabbccdd)
    remaining=(-100,0,7,1000)[seed%4]
    m.word(house+0x24c,5000)
    m.word(0x103f010,remaining)
    m.cpu.reg_write(UC_X86_REG_ESI,house)
    m.cpu.reg_write(UC_X86_REG_ESP,0x103f000)
    m.cpu.emu_start(0x5b7ef1,0x5b7f3d,count=10000)
    assert bytes(m.cpu.mem_read(scenario+0x218,1012))==rng
    actual=[m.read(house+0x24c)]
    for i in range(4):
        count=(seed*(i+1)+i)%7
        array=house+0x10000+i*0x100
        actual.append(sum(m.read(array+j*4)!=0 for j in range(count)))
        assert m.read(array+count*4)==0xaabbccdd
    expected.append(' '.join(map(str,actual)))
actual=subprocess.check_output([native],text=True).splitlines()
assert actual==expected and len(actual)==64
print('64 Battle House traces add unused cash and clear four counter arrays in original x86 order')

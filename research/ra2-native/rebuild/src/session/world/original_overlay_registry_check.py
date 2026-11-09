#!/usr/bin/env python3
"""Check base OverlayType registration and its +264 index in original x86.

The real 005DC5A0 constructor and registry scan run. Its parent constructor
and object registration service are supplied. Registry capacity is explicit.
"""
import struct
import sys
from unicorn.x86_const import UC_X86_REG_ECX,UC_X86_REG_ESP
if len(sys.argv)!=2:
    raise SystemExit('usage: original_overlay_registry_check.py game.exe')
sys.argv=[sys.argv[0],sys.argv[1],'--base']
import original_move_trace as m
vector=0x1200000
m.cpu.mem_map(vector,0x1000)
m.bind(0x5d57f0,4,m.ret(0))
m.bind(0x40ffd0,4,m.ret(0))
m.word(0xa35dec,vector)
def construct():
    m.cpu.reg_write(UC_X86_REG_ECX,m.TYPE)
    m.cpu.reg_write(UC_X86_REG_ESP,0x103f000)
    m.cpu.mem_write(0x103f000,struct.pack('<II',0x103e000,0x1009000))
    m.cpu.emu_start(0x5dc5a0,0x103e000,count=100000)
for count in range(256):
    for index in range(count):
        m.word(vector+index*4,0x1100000+index*4)
    m.word(0xa35df0,256);m.word(0xa35df8,count);m.word(0xa35dfc,0)
    construct()
    assert m.read(m.TYPE+0x264)==count
    assert m.read(vector+count*4)==m.TYPE and m.read(0xa35df8)==count+1
# A duplicate pointer selects the first entry, even though another is appended.
for index in range(20):
    m.word(vector+index*4,m.TYPE if index==3 else 0x1100000+index*4)
m.word(0xa35df0,256);m.word(0xa35df8,20);m.word(0xa35dfc,0)
construct()
assert m.read(m.TYPE+0x264)==3 and m.read(vector+20*4)==m.TYPE
# A full registry with no growth rejects the append and keeps index -1.
m.word(0xa35df0,0);m.word(0xa35df8,0);m.word(0xa35dfc,0)
construct()
assert m.read(m.TYPE+0x264)==0xffffffff and m.read(0xa35df8)==0
print('base x86 OverlayType registration matches 256 indexes, duplicate and full-registry cases')

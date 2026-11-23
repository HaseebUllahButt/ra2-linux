#!/usr/bin/env python3
"""Check original base map reset before resize.

The map-reader prefix, Map virtual calls, reset loop, and Cell constructor run
in x86. INI reading, the Cell parent/registration services, and the resize
body are supplied. This does not prove what resize or later phases write.
"""
import struct
import sys
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_ECX,UC_X86_REG_ESP
if len(sys.argv)!=2:
    raise SystemExit('usage: original_reset_order_check.py game.exe')
sys.argv=[sys.argv[0],sys.argv[1],'--base']
import original_move_trace as m
map_object=0x100e000
vector=0x100c000
rectangle=0x100b000
cell=m.cell_for(20,20)
m.word(map_object,0x79a954)
assert m.read(0x79a954+0x60)==0x548490
assert m.read(0x79a954+0x70)==0x630300
m.word(map_object+0x138,vector)
m.word(map_object+0x13c,1)
m.word(vector,cell)
m.cpu.mem_write(rectangle,struct.pack('<iiii',0,0,100,100))
m.word(cell+0x140,0xffffffff)
m.word(cell+0x54,17);m.word(cell+0x58,18)
m.word(cell+0x116,20);m.word(cell+0xe4,123);m.word(cell+0xe8,456)
m.cpu.mem_write(cell+0x122,b'\xff')
m.cpu.mem_write(cell+0x124,b'\xff')
events=[]
def on_reset(cpu,address,size,data):
    assert m.cpu.reg_read(UC_X86_REG_ECX)==cell
    events.append('cell-reset')
def on_resize():
    events.append('resize')
    assert m.read(cell+0x140)==0xff800000
    assert m.read(cell+0x54)==0xffffffff and m.read(cell+0x58)==0xffffffff
    assert struct.unpack('<h',m.cpu.mem_read(cell+0x116,2))[0]==-1
    assert m.read(cell+0xe4)==0 and m.read(cell+0xe8)==0
    assert m.cpu.mem_read(cell+0x122,1)==b'\0' and m.cpu.mem_read(cell+0x124,1)==b'\0'
m.bind(0x50dd50,12,m.ret(rectangle))
m.bind(0x40ff10,0,m.ret(0))
m.bind(0x40ffd0,4,m.ret(0))
m.bind(0x5484e0,16,on_resize)
m.cpu.hook_add(UC_HOOK_CODE,on_reset,begin=0x470f10,end=0x470f10)
m.cpu.reg_write(UC_X86_REG_ECX,map_object)
m.cpu.reg_write(UC_X86_REG_ESP,0x103f000)
m.cpu.mem_write(0x103f000,struct.pack('<II',0x103e000,0x100a000))
m.cpu.emu_start(0x49cfd0,0x49d066,count=10000000)
assert events==['cell-reset','resize'],events
print('base x86 map reader resets Cell fields before calling resize')

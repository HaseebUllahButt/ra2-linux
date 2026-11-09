#!/usr/bin/env python3
"""Check fresh-size base map reset and resize in original x86.

The original reader, Cell reset, resize wrapper, resize allocation loop, Cell
constructor, and coordinate setter execute. INI lookup, memory allocation,
and Cell parent/registration services are supplied. The trace stops before
00549981 and uses an empty object vector. It does not prove later resize
work, overlays, or Battle phases.
"""
import struct
import sys
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_ESP,UC_X86_REG_EIP
if len(sys.argv)!=2:
    raise SystemExit('usage: original_resize_check.py game.exe')
sys.argv=[sys.argv[0],sys.argv[1],'--base']
import original_move_trace as m
map_object=0x100e000
vector=0x1200000
rectangle=0x100b000
heap=0x1400000
heap_size=0x1000000
m.cpu.mem_map(vector,0x100000)
m.cpu.mem_map(heap,heap_size)
m.word(map_object,0x79a954)
m.word(map_object+0x138,vector)
m.word(map_object+0x13c,262144)
# MP02T2 Size. A poisoned old target cell models reused map storage.
m.cpu.mem_write(rectangle,struct.pack('<iiii',0,0,100,100))
target_index=80*512+135
old=m.cell_for(135,80)
m.word(vector+target_index*4,old)
m.word(old+0x140,0xffffffff)
m.cpu.mem_write(old+0x124,b'\xff')
m.word(0xa40378,0)
next_allocation=[heap]
def allocate():
    size=m.argument(0)
    address=next_allocation[0]
    next_allocation[0]+=(size+511)&~511
    if next_allocation[0]>heap+heap_size:
        raise RuntimeError('resize fixture heap exhausted')
    m.cpu.mem_write(address,b'\xa5'*size)
    m.cpu.reg_write(UC_X86_REG_EAX,address)
m.bind(0x50dd50,12,m.ret(rectangle))
m.bind(0x40ff10,0,m.ret(0))
m.bind(0x40ffd0,4,m.ret(0))
m.bind(0x782017,0,allocate)
m.cpu.reg_write(UC_X86_REG_ECX,map_object)
m.cpu.reg_write(UC_X86_REG_ESP,0x103f000)
m.cpu.mem_write(0x103f000,struct.pack('<II',0x103e000,0x100a000))
m.cpu.emu_start(0x49cfd0,0x549981,count=50000000)
assert m.cpu.reg_read(UC_X86_REG_EIP)==0x549981
count=0
for index in range(262144):
    cell=m.read(vector+index*4)
    if not cell:
        continue
    count+=1
    assert not (m.read(cell+0x140)&0x180)
    assert m.read(cell+0x54)==0xffffffff and m.read(cell+0x58)==0xffffffff
    assert struct.unpack('<h',m.cpu.mem_read(cell+0x116,2))[0]==-1
    assert m.read(cell+0xe4)==0 and m.read(cell+0xe8)==0
    assert m.cpu.mem_read(cell+0x122,1)==b'\0' and m.cpu.mem_read(cell+0x124,1)==b'\0'
    assert m.read(cell+0x24)==(index%512)|((index//512)<<16)
assert count==19900 and m.read(vector+target_index*4)==old
print('base x86 reset and resize create 19900 clear map cells for MP02T2 Size')

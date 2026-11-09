#!/usr/bin/env python3
"""Check fresh-size base map reset and resize in original x86.

The original reader, Cell reset, resize wrapper, allocation loop, Cell
constructor, coordinate setter, resize suffix, and early Cell pass execute.
INI, allocation, Cell parent/registration, lighting lookups, category cache,
and region builders are supplied. Object and lighting vectors are empty.
The trace stops before tile data, overlays, and later Battle phases.
"""
import struct
import sys
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_ESP,UC_X86_REG_EIP
if len(sys.argv)!=2:
    raise SystemExit('usage: original_resize_check.py game.exe')
sys.argv=[sys.argv[0],sys.argv[1],'--base']
import original_move_trace as m
map_object=0x8324e0
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
    assert m.read(cell+0x2c)==0 and m.read(cell+0x3c)==0
    assert m.read(cell+0x38)==0xffff and m.read(cell+0x44)==0xffffffff
    assert m.read(cell+0x54)==0xffffffff and m.read(cell+0x58)==0xffffffff
    assert struct.unpack('<h',m.cpu.mem_read(cell+0x116,2))[0]==-1
    assert m.read(cell+0xe4)==0 and m.read(cell+0xe8)==0
    assert m.cpu.mem_read(cell+0x122,1)==b'\0' and m.cpu.mem_read(cell+0x124,1)==b'\0'
    assert m.read(cell+0x24)==(index%512)|((index//512)<<16)
assert count==19900 and m.read(vector+target_index*4)==old
print('base x86 reset and resize create 19900 clear map cells for MP02T2 Size')

# Continue through the fresh resize suffix. Execute 005499C0, its original
# 0054B2E0 Cell pass, and the original 00472540 Cell body. Supply the lighting
# lookup, category cache, and two later region builders. The Cells still have
# their original no-tile/no-overlay constructor values at this point.
scenario=0x2500000
rules=0x2510000
m.cpu.mem_map(scenario,0x20000)
m.word(0xa3d290,scenario)
m.word(0x839848,rules)
m.cpu.mem_write(rules+0x584,b'\0')
m.word(0xa40970,0)
m.word(0xa6e828,0)
for i,address in enumerate((0xa6e018,0xa52ea8,0xa6e0e8,0xa6cf4c,0xa52c60,
    0xa6e0e0,0xa52f84,0xa52c70,0xa52590,0xa52ef8,0xa52ef4,0xa6cb74,
    0xa6dd0c,0xa52c68,0xa52ee0)):
    m.word(address,(i+1)*100)
for address,pop in ((0x4284e0,4),(0x429a90,0),(0x54fda0,0),(0x54ebd0,0),
                    (0x563d80,4),(0x781d3b,0),(0x529590,4),(0x5385a0,4),
                    (0x478f40,0)):
    m.bind(address,pop,m.ret(0))
region_vtable=0x100d000
m.word(region_vtable+0xc,0x1013000)
m.bind(0x1013000,0,m.ret(0))
for offset in (0x88,0xa0,0xb8):
    m.word(map_object+offset,region_vtable)
m.cpu.emu_start(0x549981,0x49d066,count=200000000)
assert m.cpu.reg_read(UC_X86_REG_EIP)==0x49d066
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
assert count==19900
print('base x86 early Cell pass preserves tested fields in all 19900 fresh cells')

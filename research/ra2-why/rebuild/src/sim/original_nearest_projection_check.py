#!/usr/bin/env python3
"""Execute original nearest search with its real projection body.

Usage: original_nearest_projection_check.py executable [--base] [--edges] [--vectors]
Usable-area, rectangle, and read-only map metadata are resolved fixture inputs.
Projection itself runs unchanged, including its source flag 1000 and trial bridge bit 100.
"""
import struct
import sys
import original_nearest_cell_check as m
from original_image import load_original
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX
projection_address=0x6a6a50 if m.base else 0x6d6410
pristine=load_original(sys.argv[1])
m.cpu.mem_write(projection_address,bytes(pristine.mem_read(projection_address,3)))
projection_out=0
def projection_enter():
    global projection_out
    projection_out=m.argument()
    x,y,z=struct.unpack('<iii',m.cpu.mem_read(m.argument(1),12))
    assert z==0, 'original nearest projection input Z differs from native center()'
    m.events.append((3,x&0xffffffff,y&0xffffffff,z))
m.callbacks[projection_address]=projection_enter
def projection_return():
    assert m.cpu.reg_read(UC_X86_REG_EAX)==projection_out
    m.events.append((5,m.read(projection_out),0,0))
for address in (0x6a6bb8,0x6a6bc8) if m.base else (0x6d6578,0x6d6588):
    assert bytes(m.cpu.mem_read(address,3))==b'\xc2\x08\x00', 'original projection return layout differs'
    m.callbacks[address]=projection_return
    m.cpu.hook_add(UC_HOOK_CODE,m.hook,begin=address,end=address)
old_metadata=m.write_metadata
def metadata(address,cell):
    usable=old_metadata(address,cell)
    if m.hashed(cell)&0x1000:m.word(address+0x140,m.read(address+0x140)|0x1000)
    return usable
m.write_metadata=metadata
def lookup():
    cell=m.read(m.argument())
    address=m.GOAL if cell==m.state['goal'] else m.FALLBACK
    m.word(address+0x24,cell);metadata(address,cell)
    level=m.cpu.mem_read(address+0x11b,1)[0];level=level if level<128 else level-256
    m.events.append((4,cell,level&0xffffffff,m.read(address+0x140)))
    m.cpu.reg_write(UC_X86_REG_EAX,address)
m.bind(0x548070 if m.base else 0x5657a0,4,lookup)
total=2166136261
for seed in range(512):
    digest=m.run(seed)
    if '--vectors' in sys.argv:print(seed,f'{digest:08x}')
    total=((total^digest)*16777619)&0xffffffff
assert total==(0x267f0e59 if '--edges' in sys.argv else 0x27b77d4e), 'original nearest and projection vectors changed'
print('512 original nearest searches with real projection',f'{total:08x}',file=sys.stderr)

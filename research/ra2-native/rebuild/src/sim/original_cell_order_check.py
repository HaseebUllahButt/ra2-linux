#!/usr/bin/env python3
"""Check original Map cell iteration with resolved allocated-cell pointers.
Requires Unicorn. Usage: original_cell_order_check.py executable [--base]
The allocation shape comes from 00565C10; this harness executes the original iterator.
"""
import struct
import sys
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_ESP
from original_image import load_original
base='--base' in sys.argv
cpu=load_original(sys.argv[1]); cpu.mem_map(0x1000000,0x200000)
MAP,GRID,STOP,STACK=0x1000000,0x1040000,0x11fe000,0x11ff000

def word(address,value): cpu.mem_write(address,struct.pack('<I',value&0xffffffff))
def call(address):
    word(STACK,STOP); cpu.reg_write(UC_X86_REG_ESP,STACK); cpu.reg_write(UC_X86_REG_ECX,MAP)
    cpu.emu_start(address,STOP,count=10000)
    return cpu.reg_read(UC_X86_REG_EAX)
for width,height,expected in ((2,1,0x60f95c5b),(3,2,0xc43f5a1f),(80,102,0xf5c43f0b),(105,110,0x94212cff),(94,53,0xbece1cd3)):
    grid=bytearray(512*512*4)
    for y in range(width+height+1):
        for x in range(width+height+1):
            if x+y>width and x-y<width and y-x<width and x+y<=width+2*height:
                struct.pack_into('<I',grid,(y*512+x)*4,x|(y<<16))
    cpu.mem_write(GRID,bytes(grid)); word(MAP+(0xf0 if base else 0xf4),width)
    word(MAP+(0x138 if base else 0x13c),GRID)
    call(0x55a550 if base else 0x578350)
    h=2166136261; count=0
    while True:
        value=call(0x55a490 if base else 0x578290)
        if not value: break
        for n in (value&65535,value>>16): h=((h^n)*16777619)&0xffffffff
        count+=1
        assert count<=512*512
    assert count==(2*width-1)*height and h==expected
    print(width,height,count,f'{h:08x}')

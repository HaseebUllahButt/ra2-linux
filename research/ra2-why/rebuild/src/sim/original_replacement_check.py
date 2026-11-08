#!/usr/bin/env python3
"""Check original tile replacement decisions with resolved disjoint catalog ranges.
Requires Unicorn. Usage: original_replacement_check.py executable [--base]
Original direction initialization and all decision instructions execute unchanged.
The map adapter supplies exact neighbor index/ramp inputs. Metadata is already loaded.
"""
import itertools
import struct
import sys
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_ESP
from original_image import load_original
base='--base' in sys.argv
cpu=load_original(sys.argv[1]); cpu.mem_map(0x1000000,0x100000)
CELL,TILE,VT,TABLE,STOP,STACK=0x1000000,0x1004000,0x1005000,0x1040000,0x10fe000,0x10ff000

def word(address,value): cpu.mem_write(address,struct.pack('<I',value&0xffffffff))
word(STACK,STOP); cpu.reg_write(UC_X86_REG_ESP,STACK)
cpu.emu_start(0x491620 if base else 0x49f2f0,STOP,count=10000)
word(CELL+0x24,50|(50<<16)); word(TILE,VT); word(VT+0x9c,0x10fd000)
cpu.mem_write(0x10fd000,b'\xb8\x01\x00\x00\x00\xc3')
# The base TileType array pointer is filled below from the verified body.
word(0xa40cd4 if base else 0xa8ed2c,TABLE)
cpu.mem_write(TABLE,struct.pack('<I',TILE)*2048)
base_globals=(0xa6e018,0xa52ea8,0xa6e0e8,0xa6cf4c,0xa52c60,0xa6e0e0,0xa52f84,0xa52c70,0xa52590,0xa52ef8,0xa52ef4,0xa6cb74,0xa6dd0c,0xa52c68,0xa52ee0)
exp_globals=(0xabc1d8,0xaa1058,0xabc2b8,0xabb104,0xaa0e18,0xabc2b0,0xaa1134,0xaa0e24,0xaa0748,0xaa10a8,0xaa10a4,0xabad28,0xabbec8,0xaa0e20,0xaa1090)
for i,address in enumerate(base_globals if base else exp_globals): word(address,(i+1)*100)
neighbors={(50,49):0x1001000,(51,50):0x1001200,(50,51):0x1001400,(49,50):0x1001600}

def map_lookup(cpu,address,size,user):
    pointer=struct.unpack('<I',cpu.mem_read(cpu.reg_read(UC_X86_REG_ESP)+4,4))[0]
    key=struct.unpack('<hh',cpu.mem_read(pointer,4))
    cpu.reg_write(UC_X86_REG_EAX,neighbors[key])
lookup=0x548070 if base else 0x5657a0
cpu.mem_write(lookup,b'\xc2\x04\x00'); cpu.hook_add(UC_HOOK_CODE,map_lookup,begin=lookup,end=lookup)
h=2166136261; count=0

def run(index,ramp,adjacent):
    global h,count
    word(CELL+0x38,index); cpu.mem_write(CELL+0x11c,bytes([ramp]))
    for address,(neighbor,neighbor_ramp) in zip(neighbors.values(),adjacent):
        word(address+0x38,neighbor); cpu.mem_write(address+0x11c,bytes([neighbor_ramp]))
    word(STACK,STOP); cpu.reg_write(UC_X86_REG_ESP,STACK); cpu.reg_write(UC_X86_REG_ECX,CELL)
    cpu.emu_start(0x471d10 if base else 0x47ca80,STOP,count=10000)
    result=struct.unpack('<I',cpu.mem_read(CELL+0x38,4))[0]
    h=((h^result)*16777619)&0xffffffff; count+=1
for group in range(4):
    center=(group+3)*100; border=(group+7)*100
    choices=[60,center,border+7]
    if group==2: choices += [1200+41,1500+1]
    if group==3: choices += [1100+13,1300+20,1400+13]
    for index in (center,border,border+7,border+15):
        for adjacent in itertools.product(choices,repeat=4): run(index,0,[(n,0) for n in adjacent])
for index in list(range(100,120))+list(range(200,212)):
    for ramp in range(21):
        for mask in range(16): run(index,ramp,[(60,0 if mask&(1<<d) else 1) for d in range(4)])
assert count==19084 and h==0x60dc1ec1
print('original tile replacement cases',count,'hash',f'{h:08x}')

#!/usr/bin/env python3
"""Check the original cliff-back branch at 0047DB59 with resolved TMP land inputs.
Requires Unicorn. Usage: original_land_check.py /path/to/executable [--base]
Execution stops before 0047DD34's passability refresh. No terrain decision is substituted.
"""
import struct
import sys
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_ESP
from original_image import load_original
base="--base" in sys.argv
cpu=load_original(sys.argv[1]); cpu.mem_map(0x1000000,0x40000)
CELL,RULES=0x1000000,0x1001000
cpu.mem_write(0x839848 if base else 0x8871e0,struct.pack('<I',RULES))
cpu.mem_write(CELL+0x24,struct.pack('<hh',50,50))
cpu.mem_write(CELL+0x11b,b'\0')
neighbor_offsets=((0,-1),(-1,0),(2,2),(1,1),(-1,1),(1,-1))
addresses={(50+x,50+y):0x1002000+i*0x200 for i,(x,y) in enumerate(neighbor_offsets)}

def map_lookup(cpu,address,size,user):
    point=struct.unpack('<I',cpu.mem_read(cpu.reg_read(UC_X86_REG_ESP)+4,4))[0]
    x,y=struct.unpack('<hh',cpu.mem_read(point,4))
    cpu.reg_write(UC_X86_REG_EAX,addresses[(x,y)])
cpu.mem_write(0x548070 if base else 0x5657a0,b'\xc2\x04\x00')
cpu.hook_add(UC_HOOK_CODE,map_lookup,begin=0x548070 if base else 0x5657a0,end=0x548070 if base else 0x5657a0)
original_mapping=(0,8,8,8,8,10,9,3,3,2,6,1,1,0,7,3)
assert struct.unpack('<16i',cpu.mem_read(0x7de4ec if base else 0x8288e4,64))==original_mapping
replay_hash=2166136261; count=0
for raw in range(16):
    for mode in (0,1,2,3):
        cpu.mem_write(RULES+(0x584 if base else 0x664),bytes([mode]))
        for mask in range(64):
            for i,address in enumerate(addresses.values()): cpu.mem_write(address+0x11b,bytes([4 if mask&(1<<i) else 0]))
            cpu.mem_write(CELL+0xec,struct.pack('<i',original_mapping[raw]))
            cpu.reg_write(UC_X86_REG_ESI,CELL); cpu.reg_write(UC_X86_REG_EDI,CELL+0x24)
            cpu.reg_write(UC_X86_REG_ESP,0x103f000)
            cpu.emu_start(0x472de7 if base else 0x47db59,0x472fc2 if base else 0x47dd34,count=10000)
            value=struct.unpack('<I',cpu.mem_read(CELL+0xec,4))[0]
            replay_hash=((replay_hash^value)*16777619)&0xffffffff; count+=1
assert count == 4096 and replay_hash == 0xbc614341
print('original cliff-back cases',count,'hash',f'{replay_hash:08x}')

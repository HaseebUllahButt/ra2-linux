#!/usr/bin/env python3
"""Recover original cell floor/ramp planes and heights. Requires Unicorn.
Usage: original_floor_check.py /path/to/gamemd.exe
"""
import struct
import sys
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_FPCW
from original_image import load_original
cpu=load_original(sys.argv[1])
cpu.mem_map(0x1000000,0x20000)
cpu.reg_write(UC_X86_REG_FPCW,0xe7f)

def call(address,argument=None,this=None):
    cpu.reg_write(UC_X86_REG_ESP,0x101f000)
    stack=struct.pack('<I',0x101e000)
    if argument is not None: stack+=struct.pack('<I',argument)
    cpu.mem_write(0x101f000,stack)
    if this is not None: cpu.reg_write(UC_X86_REG_ECX,this)
    try: cpu.emu_start(address,0x101e000,count=100000)
    except Exception:
        print('initializer dependency at',hex(cpu.reg_read(UC_X86_REG_EIP)))
        raise
    return cpu.reg_read(UC_X86_REG_EAX)

for entry in (0x47b150,0x47b1e0,0x47b200,0x47b220,0x47b2c0):
    call(entry)
level=struct.unpack('<i',cpu.mem_read(0x89e7c0,4))[0]
bridge=struct.unpack('<i',cpu.mem_read(0x89e7b4,4))[0]
print('original LevelHeight',level,'bridge height',bridge)
cpu.mem_write(0x1000000+0x11b,b'\x00')
cpu.mem_write(0x1001000,struct.pack('<iii',128,128,0))
call(0x47b3a0,0x1001000,0x1000000)
for ramp in range(20):
    plane=struct.unpack('<5d',cpu.mem_read(0x81c900+ramp*40,40))
    print('plane',ramp+1,plane)
replay_hash=2166136261
count=0
for height in (-1,0,1,7):
    cpu.mem_write(0x1000000+0x11b,bytes([height & 255]))
    for ramp in range(21):
        cpu.mem_write(0x1000000+0x11c,bytes([ramp]))
        for x,y in ((0,0),(0,255),(255,0),(255,255),(128,128),(64,192),(192,64),(-1,-1)):
            cpu.mem_write(0x1001000,struct.pack('<iii',x,y,0))
            value=call(0x47b3a0,0x1001000,0x1000000)
            replay_hash=((replay_hash^value)*16777619)&0xffffffff
            count+=1
assert count == 672 and replay_hash == 0xdbba8a41
print('original floor cases',count,'hash',f'{replay_hash:08x}')

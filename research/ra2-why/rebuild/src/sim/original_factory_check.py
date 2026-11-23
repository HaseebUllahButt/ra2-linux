#!/usr/bin/env python3
"""Check original 54-stage factory updates with a resolved cash-only adapter.
Requires Unicorn. Usage: original_factory_check.py /path/to/gamemd.exe [--base]
Cost 900 comes from original HTNK rules; interval 2 is a resolved 108-frame build input.
"""
import pathlib
import struct
import sys
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_ESP
image = pathlib.Path(sys.argv[1]).read_bytes()
base = '--base' in sys.argv
entry, frame_address, debit = (0x4b9300,0xa40d2c,0x4e5280) if base else (0x4c9b20,0xa8ed84,0x4f9790)
timer, interval, step, active, cost, special, house, suspended = (0x28,0x34,0x38,0x54,0x5c,0x64,0x68,0x6c) if base else (0x2c,0x38,0x3c,0x58,0x60,0x68,0x6c,0x70)
cpu = Uc(UC_ARCH_X86, UC_MODE_32)
cpu.mem_map(0x400000, 0x500000)
cpu.mem_write(0x400000, image[:0x500000])
cpu.mem_map(0xa00000, 0x200000)
cpu.mem_map(0x1000000, 0x20000)

def word(address, value):
    cpu.mem_write(address, struct.pack('<I',value & 0xffffffff))

def read(address):
    return struct.unpack('<I',cpu.mem_read(address,4))[0]

# Resolved cash adapter. Original factory code performs the accounting decisions.
word(0x1004024,0x1006000)
word(0x1006018,0x1010000)
cpu.mem_write(0x1010000,b'\xa1'+struct.pack('<I',0x1007000)+b'\xc2\x04\x00')
cpu.mem_write(debit,b'\x8b\x44\x24\x04\x29\x05'+struct.pack('<I',0x1007000)+b'\x01\x05'+struct.pack('<I',0x1007004)+b'\xc2\x04\x00')

def begin(cash, period):
    cpu.mem_write(0x1000000,b'\0'*0x80)
    word(0x1000000+timer,0)
    word(0x1000000+timer+8,period)
    word(0x1000000+interval,period)
    word(0x1000000+step,1)
    word(0x1000000+active,0x1002000)
    word(0x1000000+cost,900)
    word(0x1000000+special,-1)
    word(0x1000000+house,0x1004000)
    word(0x1007000,cash)
    word(0x1007004,0)

def tick(frame):
    word(frame_address,frame)
    cpu.reg_write(UC_X86_REG_ECX,0x1000000)
    cpu.reg_write(UC_X86_REG_ESP,0x101f000)
    word(0x101f000,0x101e000)
    cpu.emu_start(entry,0x101e000,count=10000)
    return (read(0x1000024),read(0x1000000+cost),read(0x1007000),read(0x1007004))

begin(900,2)
result_hash=2166136261
for frame in range(1,109):
    state=tick(frame)
    for value in state:
        result_hash=((result_hash^value)*16777619)&0xffffffff
assert state==(54,0,0,900)
assert cpu.mem_read(0x1000000+suspended,1)==b'\x01'
assert result_hash == 0x72681cc7
print('Factory 108-frame replay hash:',hex(result_hash))
begin(0,1)
assert tick(1)==(0,900,0,0)
word(0x1007000,900)
assert tick(2)==(1,884,884,16)
print('Original factory stalls and resumes with the resolved cash adapter')

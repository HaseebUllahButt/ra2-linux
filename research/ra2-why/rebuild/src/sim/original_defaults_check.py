#!/usr/bin/env python3
"""Check original Cell constructor defaults without starting the game.
Requires Unicorn. Usage: original_defaults_check.py executable [--base]
"""
import struct
import sys
from unicorn.x86_const import UC_X86_REG_ECX,UC_X86_REG_ESP
from original_image import load_original
base='--base' in sys.argv
cpu=load_original(sys.argv[1]); cpu.mem_map(0x1000000,0x20000)
cell,stack,stop=0x1000000,0x101f000,0x101e000
cpu.mem_write(cell,b'\xa5'*0x148)
cpu.mem_write(stack,struct.pack('<I',stop)); cpu.reg_write(UC_X86_REG_ESP,stack); cpu.reg_write(UC_X86_REG_ECX,cell)
cpu.emu_start(0x470f10 if base else 0x47bbf0,stop,count=10000)
def word(offset): return struct.unpack('<I',cpu.mem_read(cell+offset,4))[0]
def byte(offset): return cpu.mem_read(cell+offset,1)[0]
values=(word(0x38),byte(0x11a),byte(0x11b),byte(0x11c),word(0x44),word(0xec),int(bool(word(0x140)&0x100)),byte(0x119))
assert values==(0xffff,0,0,0,0xffffffff,0,0,0),values
print('original Cell defaults',values)

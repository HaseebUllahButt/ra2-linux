#!/usr/bin/env python3
"""Compare multiplayer house field assignment with original x86. Needs Unicorn."""
import pathlib
import struct
import subprocess
import sys
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_ESP
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parent.parent/'sim'))
from original_image import load_original
if len(sys.argv)!=3:
    raise SystemExit('usage: original_house_check.py game.exe house_cases')
exe,native=sys.argv[1:]
base=pathlib.Path(exe).name.lower()=='game.exe'
entry=0x4e8300 if base else 0x4fce00
initial,cash,color=(0x124,0x24c,0x15f40) if base else (0x1dc,0x30c,0x16054)
cpu=load_original(exe)
cpu.mem_map(0x1000000,0x40000)
house,house_type,stack,end=0x1000000,0x1020000,0x103e000,0x103f000
def put(addr,*values):cpu.mem_write(addr,struct.pack('<'+'I'*len(values),*values))
def get(addr):return struct.unpack('<I',cpu.mem_read(addr,4))[0]
put(house+0x34,house_type)
lines=subprocess.check_output([native],text=True).splitlines()
for line in lines:
    credits,chosen,expected_initial,expected_cash,expected_color,expected_type_color=map(int,line.split())
    put(stack,end,chosen,0,credits)
    cpu.reg_write(UC_X86_REG_ECX,house)
    cpu.reg_write(UC_X86_REG_ESP,stack)
    cpu.emu_start(entry,end,count=100)
    assert (get(house+initial),get(house+cash),get(house+color),get(house_type+0xc0)) == \
           (expected_initial,expected_cash,expected_color,expected_type_color)
print(f'{len(lines)} multiplayer house assignments match original x86: {pathlib.Path(exe).name}')

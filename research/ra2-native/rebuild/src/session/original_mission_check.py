#!/usr/bin/env python3
"""Check starting-unit mission assignment with a resolved house-control flag."""
import pathlib
import struct
import subprocess
import sys
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_EBX,UC_X86_REG_EDI,UC_X86_REG_ESP
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parent.parent/'sim'))
from original_image import load_original
if len(sys.argv)!=3: raise SystemExit('usage: original_mission_check.py game.exe mission_cases')
exe,native=sys.argv[1:]
base=pathlib.Path(exe).name.lower()=='game.exe'
entry,stop,control,slot=((0x5b838e,0x5b83b3,0x4f52f0,0x1d8) if base else
                         (0x5d740f,0x5d7434,0x50b730,0x1f0))
cpu=load_original(exe);cpu.mem_map(0x1000000,0x40000)
unit,vtable,stub,stack=0x1000000,0x1001000,0x1002000,0x103e000
def put(addr,value):cpu.mem_write(addr,struct.pack('<I',value))
put(unit,vtable);put(vtable+slot,stub)
cpu.mem_write(control,b'\xc3');cpu.mem_write(stub,b'\xc2\x04\x00')
human=0; missions=[]
def control_hook(cpu,address,size,user):cpu.reg_write(UC_X86_REG_EAX,human)
def mission_hook(cpu,address,size,user):
    missions.append(struct.unpack('<I',cpu.mem_read(cpu.reg_read(UC_X86_REG_ESP)+4,4))[0])
cpu.hook_add(UC_HOOK_CODE,control_hook,begin=control,end=control)
cpu.hook_add(UC_HOOK_CODE,mission_hook,begin=stub,end=stub)
for line in subprocess.check_output([native],text=True).splitlines():
    human,expected=map(int,line.split());missions=[]
    cpu.reg_write(UC_X86_REG_EBX,0);cpu.reg_write(UC_X86_REG_EDI,unit);cpu.reg_write(UC_X86_REG_ESP,stack)
    cpu.emu_start(entry,stop,count=100)
    assert missions==[expected],(human,missions,expected)
print(f'Human and AI starting missions match original x86: {pathlib.Path(exe).name}')

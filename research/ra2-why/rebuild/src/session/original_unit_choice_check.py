#!/usr/bin/env python3
"""Compare the original start-unit choice with prefiltered candidate lists.

Requires Unicorn. The original choice and RNG instructions execute. Candidate
lists and the current budget are supplied at the choice boundary.
"""
import pathlib
import struct
import subprocess
import sys
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX,
    UC_X86_REG_EBP, UC_X86_REG_ESI, UC_X86_REG_ESP)
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parent.parent/'sim'))
from original_image import load_original
if len(sys.argv)!=3: raise SystemExit('usage: original_unit_choice_check.py game.exe unit_choice_cases')
exe,native=sys.argv[1:]
base=pathlib.Path(exe).name.lower()=='game.exe'
entry,stop,constructor,scenario_global=((0x5b8282,0x5b82da,0x638790,0xa3d290) if base else
                                         (0x5d732f,0x5d7387,0x65c6d0,0xa8b230))
initial_offset,infantry_count_offset,vehicle_ptr_offset,infantry_ptr_offset=((0x14,0x2c,0x38,0x20) if base else
                                                                             (0x10,0x28,0x34,0x1c))
cpu=load_original(exe)
cpu.mem_map(0x1000000,0x40000)
scenario,vehicle_list,infantry_list,stack,end=0x1000000,0x1005000,0x1006000,0x103e000,0x103f000
def put(addr,*values):cpu.mem_write(addr,struct.pack('<'+'I'*len(values),*(v&0xffffffff for v in values)))
def get(addr,n=1):return struct.unpack('<'+'I'*n,cpu.mem_read(addr,4*n))
put(scenario_global,scenario)
lines=subprocess.check_output([native],text=True).splitlines()
for line in lines:
    values=list(map(int,line.split()))
    seed,initial,remaining,vehicles,infantry,group,index,first,second=values[:9]
    expected_table=tuple(values[9:])
    assert len(expected_table)==250
    put(stack,end,seed)
    cpu.reg_write(UC_X86_REG_ECX,scenario+0x218)
    cpu.reg_write(UC_X86_REG_ESP,stack)
    cpu.emu_start(constructor,end,count=100000)
    put(stack+initial_offset,initial)
    put(stack+infantry_count_offset,infantry)
    put(stack+vehicle_ptr_offset,vehicle_list)
    put(stack+infantry_ptr_offset,infantry_list)
    cpu.reg_write(UC_X86_REG_ESP,stack)
    cpu.reg_write(UC_X86_REG_EBP,20)
    cpu.reg_write(UC_X86_REG_ESI,vehicles)
    cpu.reg_write(UC_X86_REG_ECX,remaining)
    cpu.emu_start(entry,stop,count=10000)
    selected=cpu.reg_read(UC_X86_REG_EAX)
    selected_list=cpu.reg_read(UC_X86_REG_EDX)
    assert (selected_list,selected)==((vehicle_list if group else infantry_list),index),values[:9]
    assert get(scenario+0x218+4,2)==(first,second),values[:9]
    assert get(scenario+0x218+12,250)==expected_table,values[:9]
print(f'{len(lines)} start-unit choices match original x86: {pathlib.Path(exe).name}')

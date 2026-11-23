#!/usr/bin/env python3
"""Compare the original start-unit budget prefix with resolved type costs.

Needs Unicorn. The original type Cost virtual returns a supplied resolved value.
The remaining filter and arithmetic instructions execute without substitutions.
"""
import pathlib
import struct
import subprocess
import sys
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EBP, UC_X86_REG_ESP
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parent.parent/'sim'))
from original_image import load_original
if len(sys.argv)!=3: raise SystemExit('usage: original_budget_check.py game.exe budget_cases')
exe,native=sys.argv[1:]
base=pathlib.Path(exe).name.lower()=='game.exe'
if base:
    entry,stop=0x5b7ce0,0x5b7e36
    unitcount,tech,house_array,house_count=0xa3d2d0,0x7d88d8,0xa3229c,0xa322a8
    vehicle_array,vehicle_count,infantry_array,infantry_count=0xa35d4c,0xa35d58,0xa40354,0xa40360
    rules_global,base_ptr,base_count=0x839848,0x9d4,0x9e0
    allowed,owner,level=0x5f1,0x5e8,0x55c
else:
    entry,stop=0x5d6d80,0x5d6ed6
    unitcount,tech,house_array,house_count=0xa8b270,0x822cf4,0xa8022c,0xa80238
    vehicle_array,vehicle_count,infantry_array,infantry_count=0xa83ce4,0xa83cf0,0xa8e34c,0xa8e358
    rules_global,base_ptr,base_count=0x8871e0,0xb24,0xb30
    allowed,owner,level=0x6d5,0x6cc,0x634
cpu=load_original(exe)
cpu.mem_map(0x1000000,0x40000)
rules,house_list,houses,house_types=0x1000000,0x1002000,0x1003000,0x1005000
vehicles,infantry,bases,types=0x1007000,0x1008000,0x1009000,0x1010000
vtable,cost_stub,stack=0x1020000,0x1021000,0x103e000
def put(addr,*values):cpu.mem_write(addr,struct.pack('<'+'I'*len(values),*(v&0xffffffff for v in values)))
put(rules_global,rules)
put(house_array,house_list)
put(house_count,2)
put(vehicle_array,vehicles)
put(infantry_array,infantry)
put(rules+base_ptr,bases)
put(vtable+0xac,cost_stub)
# The original virtual Cost method receives each type and returns this supplied value.
cpu.mem_write(cost_stub,b'\x8b\x81\x00\x07\x00\x00\xc3')
for i in range(2):
    put(house_list+4*i,houses+0x1000*i)
    put(houses+0x1000*i+0x34,house_types+0x1000*i)
    put(house_types+0x1000*i+0xb4,i)
lines=subprocess.check_output([native],text=True).splitlines()
for line in lines:
    data=list(map(int,line.split()))
    units,limit,mask,expected,count=data[:5]
    put(unitcount,units)
    put(tech,limit)
    for i in range(2):cpu.mem_write(house_types+0x1000*i+0x1a6,bytes([not bool(mask&(1<<i))]))
    vehicles_n=infantry_n=bases_n=0
    for i in range(count):
        enabled,owners,required,cost,vehicle,isbase=data[5+6*i:11+6*i]
        address=types+0x1000*i
        put(address,vtable)
        cpu.mem_write(address+allowed,bytes([enabled]))
        put(address+owner,owners)
        put(address+level,required)
        put(address+0x700,cost)
        if vehicle:
            put(vehicles+4*vehicles_n,address);vehicles_n+=1
            if isbase:put(bases+4*bases_n,address);bases_n+=1
        else:put(infantry+4*infantry_n,address);infantry_n+=1
    put(vehicle_count,vehicles_n)
    put(infantry_count,infantry_n)
    put(rules+base_count,bases_n)
    put(stack,0x103f000)
    cpu.reg_write(UC_X86_REG_ECX,0)
    cpu.reg_write(UC_X86_REG_ESP,stack)
    cpu.emu_start(entry,stop,count=100000)
    actual=cpu.reg_read(UC_X86_REG_EBP)
    assert actual==expected,(units,limit,mask,actual,expected)
print(f'{len(lines)} start-unit budgets match original x86: {pathlib.Path(exe).name}')

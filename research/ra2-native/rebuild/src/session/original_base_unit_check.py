#!/usr/bin/env python3
"""Compare BaseUnit choice with original x86. Requires Unicorn.

The country index is a resolved input. Only its original resolver is stubbed.
Pass the original executable and the base_unit_cases binary.
"""
import pathlib
import struct
import subprocess
import sys
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_ESP
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parent.parent/'sim'))
from original_image import load_original

if len(sys.argv)!=3:
    raise SystemExit('usage: original_base_unit_check.py game.exe base_unit_cases')
exe,native=sys.argv[1:]
base=pathlib.Path(exe).name.lower()=='game.exe'
entry,resolver=(0x4ef6f0,0x4f94d0) if base else (0x505310,0x5117d0)
cpu=load_original(exe)
cpu.mem_map(0x1000000,0x40000)
house,house_type,vector,array,types,stack,end=(0x1000000,0x1001000,0x1002000,0x1003000,
                                                0x1004000,0x103e000,0x103f000)
def put(addr,*values):
    cpu.mem_write(addr,struct.pack('<'+'I'*len(values),*(v&0xffffffff for v in values)))
put(house+0x34,house_type)
put(vector+4,array)
put(vector+0x10,5)
for i in range(5): put(array+4*i,types+0x1000*i)
cpu.mem_write(resolver,b'\xc3')
resolved_country=0
def resolve(cpu,address,size,user):
    cpu.reg_write(UC_X86_REG_EAX,resolved_country)
cpu.hook_add(UC_HOOK_CODE,resolve,begin=resolver,end=resolver)
lines=subprocess.check_output([native],text=True).splitlines()
for line in lines:
    country,scenario,expected,*masks=map(int,line.split())
    # 005117D0 resolves this country index from the house type. It is an input.
    resolved_country=country
    for i,mask in enumerate(masks): put(types+0x1000*i+(0x5e8 if base else 0x6cc),mask)
    put(stack,end,vector)
    cpu.reg_write(UC_X86_REG_ECX,house)
    cpu.reg_write(UC_X86_REG_ESP,stack)
    cpu.emu_start(entry,end,count=10000)
    result=cpu.reg_read(UC_X86_REG_EAX)
    actual=-1 if result==0 else (result-types)//0x1000
    assert actual==expected,(country,scenario,actual,expected)
print(f'{len(lines)} BaseUnit choices match original x86: {pathlib.Path(exe).name}')

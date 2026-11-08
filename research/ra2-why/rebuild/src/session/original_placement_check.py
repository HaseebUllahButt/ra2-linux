#!/usr/bin/env python3
"""Compare the original starting-unit placement search with resolved cell inputs.

Requires Unicorn. Only map validity, empty occupancy, floor height and the
unit's placement result are supplied. The original search and RNG execute.
"""
import pathlib
import struct
import subprocess
import sys
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_ESP
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parent.parent/'sim'))
from original_image import load_original
if len(sys.argv)!=3:
    raise SystemExit('usage: original_placement_check.py game.exe placement_cases')
exe,native=sys.argv[1:]
base=pathlib.Path(exe).name.lower()=='game.exe'
if base:
    entry,constructor=0x660b60,0x638790
    scenario_global,bounds=0xa3d290,0x832600
    valid,lookup,occupant,floor=0x54aab0,0x548070,0x471660,0x55a280
    valid_ret=4
else:
    entry,constructor=0x688ed0,0x65c6d0
    scenario_global,bounds=0xa8b230,0x87f90c
    valid,lookup,occupant,floor=0x578460,0x5657a0,0x47c3d0,0x578080
    valid_ret=8
cpu=load_original(exe)
cpu.mem_map(0x1000000,0x40000)
scenario,entity,vtable,origin,stack,end,stub=(0x1000000,0x1004000,0x1005000,0x1006000,
                                            0x103d000,0x103f000,0x1010000)
def put(addr,*values):cpu.mem_write(addr,struct.pack('<'+'I'*len(values),*(v&0xffffffff for v in values)))
def get(addr,n=1):return struct.unpack('<'+'I'*n,cpu.mem_read(addr,4*n))
put(scenario_global,scenario)
put(bounds,0,0,14,16)
put(entity,vtable)
put(vtable+(0xd4 if base else 0xd8),stub)
cpu.mem_write(stub,b'\xb8\x01\x00\x00\x00\xc2\x08\x00')
cpu.mem_write(valid,b'\xc2'+struct.pack('<H',valid_ret))
cpu.mem_write(lookup,b'\xc2\x10\x00')
cpu.mem_write(occupant,b'\xc3')
put(0x1009000,0x100a000)
put(0x100a000+0x2c,0x1010100)
cpu.mem_write(0x1010100,b'\x31\xc0\xc3')
cpu.mem_write(floor,b'\x31\xc0\xc2\x04\x00')
attempts=[]
wanted=0
def cell_check(cpu,address,size,user):
    pointer=get(cpu.reg_read(UC_X86_REG_ESP)+4)[0]
    attempts.append(struct.unpack('<hh',cpu.mem_read(pointer,4)))
    cpu.reg_write(UC_X86_REG_EAX,int(wanted!=0 and len(attempts)==wanted))
def cell_lookup(cpu,address,size,user):
    if base and wanted!=1:
        pointer=get(cpu.reg_read(UC_X86_REG_ESP)+4)[0]
        attempts.append(struct.unpack('<hh',cpu.mem_read(pointer,4)))
    cpu.reg_write(UC_X86_REG_EAX,0x1008000)
def occupied(cpu,address,size,user):
    rejected=base and wanted!=1 and (wanted==0 or len(attempts)!=wanted)
    cpu.reg_write(UC_X86_REG_EAX,0x1009000 if rejected else 0)
cpu.hook_add(UC_HOOK_CODE,occupied,begin=occupant,end=occupant)
cpu.hook_add(UC_HOOK_CODE,cell_check,begin=valid,end=valid)
cpu.hook_add(UC_HOOK_CODE,cell_lookup,begin=lookup,end=lookup)
lines=subprocess.check_output([native,'base' if base else 'yuri'],text=True).splitlines()
for line in lines:
    data=list(map(int,line.split()))
    seed,wanted,native_success,first,second,count=data[:6]
    coordwords=data[6:6+2*count]
    expected=list(zip(coordwords[example.invalid],coordwords[example.invalid]))
    expected_table=tuple(data[6+2*count:])
    assert len(expected_table)==250
    put(stack,end,seed)
    cpu.reg_write(UC_X86_REG_ECX,scenario+0x218)
    cpu.reg_write(UC_X86_REG_ESP,stack)
    cpu.emu_start(constructor,end,count=100000)
    cpu.mem_write(origin,struct.pack('<hh',5,6))
    put(stack,end,1)
    cpu.reg_write(UC_X86_REG_ECX,entity)
    cpu.reg_write(UC_X86_REG_EDX,origin)
    cpu.reg_write(UC_X86_REG_ESP,stack)
    attempts=[]
    cpu.emu_start(entry,end,count=1000000)
    assert cpu.reg_read(UC_X86_REG_EAX)==native_success,(seed,wanted,'result',len(attempts),attempts[:30],expected[:30])
    assert attempts==expected,(seed,wanted,attempts[:30],expected[:30])
    assert get(scenario+0x218+4,2)==(first,second),(seed,wanted,'indexes')
    assert get(scenario+0x218+12,250)==expected_table,(seed,wanted,'table')
print(f'{len(lines)} placement searches match original x86: {pathlib.Path(exe).name}')

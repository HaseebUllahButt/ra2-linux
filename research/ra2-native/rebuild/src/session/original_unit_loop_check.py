#!/usr/bin/env python3
"""Compare the original unit budget loop with resolved creation and placement.

Requires Unicorn. Candidate lists, unit Cost, creation, and placement outcomes
are supplied. The original choice, retry, budget, and RNG instructions run.
"""
import pathlib
import struct
import subprocess
import sys
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX,
    UC_X86_REG_EBP, UC_X86_REG_ESI, UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_EDI)
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parent.parent/'sim'))
from original_image import load_original
if len(sys.argv)!=3: raise SystemExit('usage: original_unit_loop_check.py game.exe unit_loop_cases')
exe,native=sys.argv[1:]
base=pathlib.Path(exe).name.lower()=='game.exe'
if base:
    entry,stop,choice=0x5b8269,0x5b83bd,0x5b82da
    finish,finish_stop=0x5b7f4b,0x5b7f63
    constructor,scenario_global=0x638790,0xa3d290
    factory,placement,human=0x783ce4,0x660b60,0x4f52f0
    budget_off,initial_off,vehicles_off,infantry_off=0x54,0x14,0x44,0x2c
    vehicle_ptr,infantry_ptr,house_off,mission_off=0x38,0x20,0x50,0x1d8
else:
    entry,stop,choice=0x5d7316,0x5d743e,0x5d7387
    finish,finish_stop=0x5d6feb,0x5d7003
    constructor,scenario_global=0x65c6d0,0xa8b230
    factory,placement,human=0x7caae4,0x688ed0,0x50b730
    budget_off,initial_off,vehicles_off,infantry_off=0x50,0x10,0x40,0x28
    vehicle_ptr,infantry_ptr,house_off,mission_off=0x34,0x1c,0x4c,0x1f0
cpu=load_original(exe)
cpu.mem_map(0x1000000,0x40000)
scenario,vehicles,infantry,types,type_vtable=0x1000000,0x1005000,0x1006000,0x1010000,0x1018000
unit,unit_vtable,house,budget_ptr,stack,end=0x1019000,0x101a000,0x101b000,0x101c000,0x103d000,0x103f000
cost_stub,type_stub,unit_stub=0x1020000,0x1020100,0x1020200
def put(addr,*values):cpu.mem_write(addr,struct.pack('<'+'I'*len(values),*(v&0xffffffff for v in values)))
def get(addr,n=1):return struct.unpack('<'+'I'*n,cpu.mem_read(addr,4*n))
put(scenario_global,scenario)
put(unit,unit_vtable)
put(type_vtable+0x8c,type_stub)
put(type_vtable+0xac,cost_stub)
put(unit_vtable+0x20,unit_stub)
put(unit_vtable+mission_off,unit_stub)
cpu.mem_write(cost_stub,b'\x8b\x81\x00\x07\x00\x00\xc3')
cpu.mem_write(type_stub,b'\x31\xc0\xc2\x04\x00')
cpu.mem_write(unit_stub,b'\xc2\x04\x00')
cpu.mem_write(factory,b'\xb8'+struct.pack('<I',unit)+b'\xc3')
cpu.mem_write(placement,b'\xc2\x04\x00')
cpu.mem_write(human,b'\x31\xc0\xc3')
if not base:
    # Resolved house waypoint pointer, followed by the original placement call.
    cpu.mem_write(0x50def0,b'\xb8'+struct.pack('<I',house+0x100)+b'\xc2\x04\x00')
for i,cost in enumerate((90,110,125,30,40,60)):
    addr=types+i*0x1000
    put(addr,type_vtable)
    put(addr+0x700,cost)
    put((vehicles+4*i) if i<3 else (infantry+4*(i-3)),addr)
attempts=[]
mode=0
def selected(cpu,address,size,user):
    pointer=cpu.reg_read(UC_X86_REG_EDX)
    index=cpu.reg_read(UC_X86_REG_EAX)
    attempts.append(index if pointer==vehicles else index+3)
def place(cpu,address,size,user):
    n=len(attempts)
    success=mode==0 or (mode==1 and n%2==0)
    cpu.reg_write(UC_X86_REG_EAX,int(success))
cpu.hook_add(UC_HOOK_CODE,selected,begin=choice,end=choice)
cpu.hook_add(UC_HOOK_CODE,place,begin=placement,end=placement)
lines=subprocess.check_output([native],text=True).splitlines()
for line in lines:
    data=list(map(int,line.split()))
    seed,budget,mode,remaining,failures,first,second,n=data[:8]
    pairs=data[8:8+2*n]
    expected_types=pairs[example.invalid]
    offset=8+2*n
    expected_table=tuple(data[offset:offset+250])
    final_draw,final_first,final_second=data[offset+250:offset+253]
    final_table=tuple(data[offset+253:])
    assert len(expected_table)==250 and len(final_table)==250
    put(stack,end,seed)
    cpu.reg_write(UC_X86_REG_ECX,scenario+0x218)
    cpu.reg_write(UC_X86_REG_ESP,stack)
    cpu.emu_start(constructor,end,count=100000)
    put(scenario,0)
    put(budget_ptr,budget)
    put(stack+budget_off,budget_ptr)
    put(stack+initial_off,budget)
    put(stack+vehicles_off,3)
    put(stack+infantry_off,3)
    put(stack+vehicle_ptr,vehicles)
    put(stack+infantry_ptr,infantry)
    put(stack+house_off,house)
    cpu.reg_write(UC_X86_REG_ESP,stack)
    cpu.reg_write(UC_X86_REG_ESI,3)
    cpu.reg_write(UC_X86_REG_EDI,0)
    attempts=[]
    cpu.emu_start(entry,stop,count=1000000)
    actual_budget=struct.unpack('<i',cpu.mem_read(budget_ptr,4))[0]
    assert (actual_budget,20-cpu.reg_read(UC_X86_REG_EBP))==(remaining,failures),(data[:8],actual_budget,20-cpu.reg_read(UC_X86_REG_EBP),attempts,hex(cpu.reg_read(UC_X86_REG_EIP)))
    assert attempts==expected_types,(data[:8],attempts,expected_types)
    assert get(scenario+0x218+4,2)==(first,second),data[:8]
    assert get(scenario+0x218+12,250)==expected_table,data[:8]
    cpu.emu_start(finish,finish_stop,count=10000)
    assert cpu.reg_read(UC_X86_REG_EAX)==final_draw,data[:8]
    assert get(scenario+0x218+4,2)==(final_first,final_second),data[:8]
    assert get(scenario+0x218+12,250)==final_table,data[:8]
print(f'{len(lines)} starting-unit loops match original x86: {pathlib.Path(exe).name}')
